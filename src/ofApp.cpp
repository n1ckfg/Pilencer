#include "ofApp.h"

using namespace cv;
using namespace ofxCv;

void ofApp::setup() {
    settings.loadFile("settings.xml");

    ofSetVerticalSync(false);    
    framerate = settings.getValue("settings:framerate", 60); 
    width = settings.getValue("settings:width", 160); 
    height = settings.getValue("settings:height", 120); 
    ofSetFrameRate(framerate);

    host = settings.getValue("settings:host", "127.0.0.1"); 
    port = settings.getValue("settings:port", 7110);
    
    debug = (bool) settings.getValue("settings:debug", 1);
    sendPosition = (bool) settings.getValue("settings:send_position", 0);

    sender.setup(host, port);

    // ~ ~ ~   get a persistent name for this computer   ~ ~ ~
    compname = "RPi";
    file.open(ofToDataPath("compname.txt"), ofFile::ReadWrite, false);
    ofBuffer buff;
    if (file) { // use existing file if it's there
        buff = file.readToBuffer();
        compname = buff.getText();
    } else { // otherwise make a new one
        compname += "_" + ofGetTimestampString("%y%m%d%H%M%S%i");
        ofStringReplace(compname, "\n", "");
        ofStringReplace(compname, "\r", "");
        buff.set(compname.c_str(), compname.size());
        ofBufferToFile("compname.txt", buff);
    }
    std::cout << compname << endl;

    cam.setup(width, height, false); // color/gray;

    triggerThreshold = settings.getValue("settings:trigger_threshold", 0.5);
    counterMax = settings.getValue("settings:trigger_frames", 3);
    timeDelay = settings.getValue("settings:time_delay", 5000);

    // ~ ~ ~   cam settings   ~ ~ ~
    camSharpness = settings.getValue("settings:sharpness", 0); 
    camContrast = settings.getValue("settings:contrast", 0); 
    camBrightness = settings.getValue("settings:brightness", 50); 
    camIso = settings.getValue("settings:iso", 300); 
    camExposureMode = settings.getValue("settings:exposure_mode", 0); 
    camExposureCompensation = settings.getValue("settings:exposure_compensation", 0); 
    camShutterSpeed = settings.getValue("settings:shutter_speed", 0);

    cam.setSharpness(camSharpness);
    cam.setContrast(camContrast);
    cam.setBrightness(camBrightness);
    cam.setISO(camIso);
    cam.setExposureMode((MMAL_PARAM_EXPOSUREMODE_T) camExposureMode);
    cam.setExposureCompensation(camExposureCompensation);
    cam.setShutterSpeed(camShutterSpeed);
    //cam.setFrameRate // not implemented in ofxCvPiCam

    // ~ ~ ~   optical flow settings   ~ ~ ~
    useFarneback = (bool) settings.getValue("settings:dense_flow", 1);
    pyrScale = 0.5;   // 0 to 1, default 0.5
    levels = 4;   // 1 to 8, default 4
    winsize = 8;   // 4 to 64, default 8
    iterations = 2;   // 1 to 8, default 2
    polyN = 7;   // 5 to 10, default 7
    polySigma = 1.5;   // 1.1 to 2, default 
    OPTFLOW_FARNEBACK_GAUSSIAN = false; // default false
    winSize = 32;   // 4 to 64, default 32
    maxLevel = 3;   // 0 to 8, default 3
    maxFeatures = 200;   // 1 to 1000, default 200
    qualityLevel = 0.01;   // 0.001 to 0.02, default 0.01
    minDistance = 4;   // 1 to 16, default 4

    motionVal = 0;
    counter = 0;
    markTime = 0;
    trigger = false;
    isMoving = false;
}

void ofApp::update() {
    frame = cam.grab();

    if (!frame.empty()) {
        if (useFarneback) {
			curFlow = &farneback;
	        farneback.setPyramidScale( pyrScale );
	        farneback.setNumLevels( levels );
	        farneback.setWindowSize( winsize );
	        farneback.setNumIterations( iterations );
	        farneback.setPolyN( polyN );
	        farneback.setPolySigma( polySigma );
	        farneback.setUseGaussian( OPTFLOW_FARNEBACK_GAUSSIAN );
		} else {
			curFlow = &pyrLk;
            pyrLk.setMaxFeatures( maxFeatures );
            pyrLk.setQualityLevel( qualityLevel );
            pyrLk.setMinDistance( minDistance );
            pyrLk.setWindowSize( winSize );
            pyrLk.setMaxLevel( maxLevel );
		}

        // you use Flow polymorphically
        curFlow->calcOpticalFlow(frame);

        if (useFarneback) {
        	motionValRaw = farneback.getAverageFlow();
    	} else {
    		motionValRaw = ofVec2f(0,0);
    		std::vector<ofVec2f> points = pyrLk.getMotion();

    		for (int i=0; i<points.size(); i++) {
    			motionValRaw.x += points[i].x;
    			motionValRaw.y += points[i].y;
    		}  
    		motionValRaw.x /= (float) points.size();
    		motionValRaw.y /= (float) points.size();
    	}

        motionVal = (abs(motionValRaw.x) + abs(motionValRaw.y)) / 2.0;
        isMoving = motionVal > triggerThreshold;
        std::cout << "val: " << motionVal << " motion: " << isMoving << endl;
   
        int t = ofGetElapsedTimeMillis();

    	if (!trigger && isMoving) { // motion detected, but not triggered yet
        	if (counter < counterMax) { // start counting frames
        		counter++;
        	} else { // motion frames have reached trigger threshold
                markTime = t;
	        	trigger = true;
	        }  
        } else if (trigger && isMoving) { // triggered, reset timer as long as motion is detected
            markTime = t;
    	} else if (trigger && !isMoving) {
    		counter = 0;
	    	curFlow->resetFlow();
	     	if (t > markTime + timeDelay) { // triggered, timer has run out
    			trigger = false;
    		}
        }

        sendOsc();
    }
}

void ofApp::draw() {   
    if (debug) {
    	ofSetColor(255);
    	ofBackground(0);

        if(!frame.empty()) {
    	    drawMat(frame,0, 0);
    	    curFlow->draw(0, 0);
    	}

        stringstream info;
        info << "FPS: " << ofGetFrameRate() << endl;
        ofDrawBitmapStringHighlight(info.str(), 10, 10, ofColor::black, ofColor::yellow);
    }
}


void ofApp::sendOsc() {
	ofxOscMessage msg;
    msg.setAddress("/pilencer");
    msg.addStringArg(compname);
    msg.addIntArg((int) trigger);
    
    if (sendPosition) {
	    msg.addFloatArg(motionVal);
    	msg.addFloatArg(motionValRaw.x);
    	msg.addFloatArg(motionValRaw.y);
	}

    sender.sendMessage(msg);
    std:cout << "*** SENT: " << trigger << " ***\n";
}
