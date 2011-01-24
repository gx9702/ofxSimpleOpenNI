#include "ofxSimpleOpenNI.h"

// Callback: New user
void XN_CALLBACK_TYPE User_NewUser(UserGenerator& generator, XnUserID nId, void* pCookie)
{
	ofxSimpleOpenNI* app = (ofxSimpleOpenNI*) pCookie;

	printf("New User %d\n", nId);
	// New user found

	if (app->g_bNeedPose)
	{
		app->g_user.GetPoseDetectionCap().StartPoseDetection(app->g_strPose, nId);
	}
	else
	{
		app->g_user.GetSkeletonCap().RequestCalibration(nId,true);
	}
}

// Callback: An existing user was lost
void XN_CALLBACK_TYPE User_LostUser(UserGenerator& generator, XnUserID nId, void* pCookie)
{
	ofxSimpleOpenNI* app = (ofxSimpleOpenNI*) pCookie;

	printf("Lost user %d\n", nId);
}

// Callback: Detected a pose
void XN_CALLBACK_TYPE UserPose_PoseDetected(PoseDetectionCapability& capability, const XnChar* strPose, XnUserID nId, void* pCookie)
{
	ofxSimpleOpenNI* app = (ofxSimpleOpenNI*) pCookie;
	
	printf("Pose %s detected for user %d\n", strPose, nId);
	app->g_user.GetPoseDetectionCap().StopPoseDetection(nId);
	app->g_user.GetSkeletonCap().RequestCalibration(nId, true);
}

// Callback: Started calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(SkeletonCapability& capability, XnUserID nId, void* pCookie)
{
	ofxSimpleOpenNI* app = (ofxSimpleOpenNI*) pCookie;
	
	printf("Calibration started for user %d\n", nId);
}

// Callback: Finished calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationEnd(SkeletonCapability& capability, XnUserID nId, XnBool bSuccess, void* pCookie)
{
	ofxSimpleOpenNI* app = (ofxSimpleOpenNI*) pCookie;
	
	if (bSuccess)
	{
		// Calibration succeeded
		printf("Calibration complete, start tracking user %d\n", nId);
		app->g_user.GetSkeletonCap().StartTracking(nId);
	}
	else
	{
		// Calibration failed
		printf("Calibration failed for user %d\n", nId);
		if (app->g_bNeedPose)
		{
			app->g_user.GetPoseDetectionCap().StartPoseDetection(app->g_strPose, nId);
		}
		else
		{
			app->g_user.GetSkeletonCap().RequestCalibration(nId, TRUE);
		}
	}
}

//--------------------------------------------------------------
void ofxSimpleOpenNI::setup()
{
        XnStatus rc;
        EnumerationErrors errors;

	g_bNeedPose = false;
        
	//rc = g_context.InitFromXmlFile(SAMPLE_XML_PATH, &errors);
	rc = g_context.InitFromXmlFile(SAMPLE_LICENSE_PATH, &errors);
	rc = g_context.RunXmlScriptFromFile(SAMPLE_LICENSE_PATH);
        if (rc == XN_STATUS_NO_NODE_PRESENT)
        {
                XnChar strError[1024];
                errors.ToString(strError, 1024);
                printf("%s\n", strError);
        }
        else if (rc != XN_STATUS_OK)
        {
                printf("Open failed: %s\n", xnGetStatusString(rc));
        }

        rc = g_context.OpenFileRecording(SAMPLE_RECORDING_PATH);         
        
	rc = g_context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_depth);
        rc = g_context.FindExistingNode(XN_NODE_TYPE_IMAGE, g_image);
	rc = g_context.FindExistingNode(XN_NODE_TYPE_USER, g_user);

	if (rc != XN_STATUS_OK)
	{
		rc = g_user.Create(g_context);
		CHECK_RC(rc, "Find user generator");
	}

	XnCallbackHandle hUserCallbacks, hCalibrationCallbacks, hPoseCallbacks;

	if (!g_user.IsCapabilitySupported(XN_CAPABILITY_SKELETON))
	{
		printf("Supplied user generator doesn't support skeleton\n");
	}

	g_user.RegisterUserCallbacks(User_NewUser,User_LostUser,this, hUserCallbacks);

	g_user.GetSkeletonCap().RegisterCalibrationCallbacks(UserCalibration_CalibrationStart,UserCalibration_CalibrationEnd,this,hCalibrationCallbacks);
	
	if (g_user.GetSkeletonCap().NeedPoseForCalibration())
	{
		g_bNeedPose = true;
		if (!g_user.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION))
		{
			printf("Pose required, but not supported\n");
		}
		g_user.GetPoseDetectionCap().RegisterToPoseCallbacks(UserPose_PoseDetected,NULL,this,hPoseCallbacks);
		g_user.GetSkeletonCap().GetCalibrationPose(g_strPose);
	}

	g_user.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);
	
	XnFieldOfView FOV;
	xnGetDepthFieldOfView(g_depth, &FOV);

	fXtoZ = tan(FOV.fHFOV/2)*2;
	fYtoZ = tan(FOV.fVFOV/2)*2;

	width = WIDTH;
        height = HEIGHT;

        texDepth.allocate(width,height,GL_LUMINANCE16);
        texColor.allocate(width,height,GL_RGB);
        texUser.allocate(width,height,GL_LUMINANCE16);

	texDepth.texData.pixelType = GL_UNSIGNED_SHORT;
	texDepth.texData.glType = GL_LUMINANCE;
	texDepth.texData.glTypeInternal = GL_LUMINANCE16;
	//texColor.texData.pixelType = GL_UNSIGNED_BYTE;
	//texColor.texData.glType = GL_RGB;
	//texColor.texData.glTypeInternal = GL_RGB;
	texUser.texData.pixelType = GL_UNSIGNED_SHORT;
	texUser.texData.glType = GL_LUMINANCE;
	texUser.texData.glTypeInternal = GL_LUMINANCE16;
	
	texDepth.setTextureMinMagFilter(GL_LINEAR,GL_NEAREST);
	texColor.setTextureMinMagFilter(GL_LINEAR,GL_LINEAR);
	texUser.setTextureMinMagFilter(GL_LINEAR,GL_NEAREST);

	rc = g_context.StartGeneratingAll();

        // Read a new frame
	rc = g_context.WaitAndUpdateAll();
        if (rc != XN_STATUS_OK)
        {
                printf("Read failed: %s\n", xnGetStatusString(rc));
        }

        g_depth.GetAlternativeViewPointCap().SetViewPoint(g_image);

	ofDisableTextureEdgeHack();
	
	nbVertices = NBPOINTS;
	sizeVertices = 3*nbVertices*sizeof(GLfloat);

	pointCloud.reserve(width*height);
	
	shader.setup(string("myShader"),string("myShader"));
	initShape();
}

void ofxSimpleOpenNI::initShape()
{
	 pointCloud.begin(GL_POINTS);

	unsigned int step = 1;
       
	for(int y = 0; y < height; y += step)
	{
      		for(int x = 0; x < width; x += step)
		{
			pointCloud.addVertex(x,y);
			pointCloud.setTexCoord(x,y);
		}
	}
	pointCloud.end();
}

void ofxSimpleOpenNI::updateShape()
{
	pointCloud.begin(GL_POINTS);

	unsigned int step = 1;
       
	for(int y = 0; y < height; y += step)
	{
      		for(int x = 0; x < width; x += step)
		{
			XnPoint3D pt;
			pt.X=x;
			pt.Y=y;
			pt.Z=g_depthMD(x,y);
			
			//g_depth.ConvertProjectiveToRealWorld(1,&pt,&pt);

			pointCloud.addVertex(pt.X,pt.Y,pt.Z);
			pointCloud.setTexCoord(x,y);
		}
	}
	pointCloud.end();
}

//--------------------------------------------------------------
void ofxSimpleOpenNI::update()
{
        XnStatus rc;
        
	// Read a new frame
        //rc = g_context.WaitAnyUpdateAll();
	rc = g_context.WaitAndUpdateAll();
        
	if (rc != XN_STATUS_OK)
        {
                printf("Read failed: %s\n", xnGetStatusString(rc));
        }

        g_depth.GetMetaData(g_depthMD);
        g_image.GetMetaData(g_imageMD);
	g_user.GetUserPixels(0,g_sceneMD);

      	depthPixels = g_depthMD.Data();
        colorPixels = g_imageMD.Data();
	userPixels = g_sceneMD.Data();

	int pos=0;

	/*
        unsigned char*	color;
	color = (unsigned char*) malloc(width*height*3*sizeof(unsigned char));

	for (int nY=0; nY<height; nY++)
	{
		for (int nX=0; nX < width; nX++)
		{
			pos = nY*width+nX;		
			//color[pos*3+0] = colorPixels[pos*3+0];
			//color[pos*3+1] = colorPixels[pos*3+1];
			//color[pos*3+2] = colorPixels[pos*3+2];
			
			color[pos*3+0] = 0.;
			color[pos*3+1] = 0.;
			color[pos*3+2] = 0.;

			if (userPixels[pos] != 0)
			{
				color[pos*3+0] = colorPixels[pos*3+0];
				color[pos*3+1] = colorPixels[pos*3+1];
				color[pos*3+2] = colorPixels[pos*3+2];
	
				//color[pos*3+0] = 1.; 
				//color[pos*3+1] = 0.;
				//color[pos*3+2] = 0.;
			}
		}

	}

	//FIX: FREE Memory from color!!!
	*/

	//texDepth.loadData((unsigned short*)depthPixels,width,height,GL_LUMINANCE); 
	texDepth.loadData((unsigned char*)depthPixels,width,height,GL_LUMINANCE); 
        texColor.loadData((unsigned char*)colorPixels,width,height,GL_RGB);
        //texColor.loadData((unsigned char*)color,width,height,GL_RGB);
	//texUser.loadData((unsigned short*)userPixels,width,height,GL_LUMINANCE); 
	texUser.loadData((unsigned char*)userPixels,width,height,GL_LUMINANCE); 

	//updateShape();
}

//--------------------------------------------------------------
void ofxSimpleOpenNI::drawShape()
{
	glEnable(GL_DEPTH_TEST);

	shader.begin();

	shader.setTexture("depth",texDepth,0);
	shader.setTexture("tex",texColor,1);
	shader.setTexture("user",texUser,2);

	shader.setUniform("resolution",(float)width,(float)height);
	shader.setUniform("XYtoZ",(float)fXtoZ,(float)fYtoZ);

	pointCloud.draw();

	shader.end();

	glDisable(GL_DEPTH_TEST);

}

void ofxSimpleOpenNI::drawTexture()
{
	//texDepth.draw(5,5,width/2,height/2);
	//texUser.draw(5*2+width/2,5,width/2,height/2);
        //texColor.draw(5*3+width/2*2,5,width/2,height/2);
}
