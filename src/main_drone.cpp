/**
* This file is part of DSO.
* 
* Copyright 2016 Technical University of Munich and Intel.
* Developed by Jakob Engel <engelj at in dot tum dot de>,
* for more information see <http://vision.in.tum.de/dso>.
* If you use this code, please cite the respective publications as
* listed on the above website.
*
* DSO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* DSO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with DSO. If not, see <http://www.gnu.org/licenses/>.
*/



#include <thread>
#include <iostream>
#include <chrono>
#include <thread>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "IOWrapper/Output3DWrapper.h"
#include "IOWrapper/ImageDisplay.h"


#include <boost/thread.hpp>
#include "util/settings.h"
#include "util/globalFuncs.h"
#include "util/DatasetReader.h"
#include "util/globalCalib.h"

#include "util/NumType.h"
#include "FullSystem/FullSystem.h"
#include "OptimizationBackend/MatrixAccumulators.h"
#include "FullSystem/PixelSelector2.h"


#include "IOWrapper/Pangolin/PangolinDSOViewer.h"
#include "IOWrapper/OutputWrapper/SampleOutputWrapper.h"
#include "IOWrapper/InputDepthWrapper.h"

#include <raspicam/raspicam_cv.h>


std::string vignette, gammaCalib, source, calib, extDepthFolder;
double rescale = 1;
bool reverse = false;
bool disableROS = false;
int start=0;
int end=100000;
bool prefetch = false;
float playbackSpeed=0;	// 0 for linearize (play as fast as possible, while sequentializing tracking & mapping). otherwise, factor on timestamps.
bool preload=false;
bool useSampleOutput=false;

int extDepth_minX = -1, extDepth_maxX = -1,
    extDepth_minY = -1, extDepth_maxY = -1;

int mode=0;

bool firstRosSpin=false;

using namespace dso;


void my_exit_handler(int s)
{
	printf("Caught signal %d\n",s);
	exit(1);
}

void exitThread()
{
	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = my_exit_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);

	firstRosSpin=true;
	while(true) pause();
}



void settingsDefault(int preset)
{
	printf("\n=============== PRESET Settings: ===============\n");
	if(preset == 0 || preset == 1)
	{
		printf("DEFAULT settings:\n"
				"- %s real-time enforcing\n"
				"- 2000 active points\n"
				"- 5-7 active frames\n"
				"- 1-6 LM iteration each KF\n"
				"- original image resolution\n", preset==0 ? "no " : "1x");

		playbackSpeed = (preset==0 ? 0 : 1);
		preload = preset==1;
		setting_desiredImmatureDensity = 1500;
		setting_desiredPointDensity = 2000;
		setting_minFrames = 5;
		setting_maxFrames = 7;
		setting_maxOptIterations=6;
		setting_minOptIterations=1;

		setting_logStuff = false;
	}

	if(preset == 2 || preset == 3)
	{
		printf("FAST settings:\n"
				"- %s real-time enforcing\n"
				"- 800 active points\n"
				"- 4-6 active frames\n"
				"- 1-4 LM iteration each KF\n"
				"- 424 x 320 image resolution\n", preset==0 ? "no " : "5x");

		playbackSpeed = (preset==2 ? 0 : 5);
		preload = preset==3;
		setting_desiredImmatureDensity = 600;
		setting_desiredPointDensity = 800;
		setting_minFrames = 4;
		setting_maxFrames = 6;
		setting_maxOptIterations=4;
		setting_minOptIterations=1;

		benchmarkSetting_width = 424;
		benchmarkSetting_height = 320;

		setting_logStuff = false;
	}

	if(preset == 4)
	{
		printf("ONLINE settings:\n"
			"- 800 active points\n"
			"- 4-6 active frames\n"
			"- 1-4 LM iteration each KF\n");
		setting_desiredImmatureDensity = 600;
		setting_desiredPointDensity = 800;
		setting_minFrames = 4;
		setting_maxFrames = 6;
		setting_maxOptIterations = 4;
		setting_minOptIterations = 1;

		setting_logStuff = false;
	}

	printf("==============================================\n");
}






void parseArgument(char* arg)
{
	int option;
	float foption;
	char buf[1000];


    if(1==sscanf(arg,"sampleoutput=%d",&option))
    {
        if(option==1)
        {
            useSampleOutput = true;
            printf("USING SAMPLE OUTPUT WRAPPER!\n");
        }
        return;
    }

    if(1==sscanf(arg,"quiet=%d",&option))
    {
        if(option==1)
        {
            setting_debugout_runquiet = true;
            printf("QUIET MODE, I'll shut up!\n");
        }
        return;
    }

	if(1==sscanf(arg,"preset=%d",&option))
	{
		settingsDefault(option);
		return;
	}


	if(1==sscanf(arg,"rec=%d",&option))
	{
		if(option==0)
		{
			disableReconfigure = true;
			printf("DISABLE RECONFIGURE!\n");
		}
		return;
	}



	if(1==sscanf(arg,"noros=%d",&option))
	{
		if(option==1)
		{
			disableROS = true;
			disableReconfigure = true;
			printf("DISABLE ROS (AND RECONFIGURE)!\n");
		}
		return;
	}

	if(1==sscanf(arg,"nolog=%d",&option))
	{
		if(option==1)
		{
			setting_logStuff = false;
			printf("DISABLE LOGGING!\n");
		}
		return;
	}
	if(1==sscanf(arg,"reverse=%d",&option))
	{
		if(option==1)
		{
			reverse = true;
			printf("REVERSE!\n");
		}
		return;
	}
	if(1==sscanf(arg,"nogui=%d",&option))
	{
		if(option==1)
		{
			disableAllDisplay = true;
			printf("NO GUI!\n");
		}
		return;
	}
	if(1==sscanf(arg,"nomt=%d",&option))
	{
		if(option==1)
		{
			multiThreading = false;
			printf("NO MultiThreading!\n");
		}
		return;
	}
	if(1==sscanf(arg,"prefetch=%d",&option))
	{
		if(option==1)
		{
			prefetch = true;
			printf("PREFETCH!\n");
		}
		return;
	}
	if(1==sscanf(arg,"start=%d",&option))
	{
		start = option;
		printf("START AT %d!\n",start);
		return;
	}
	if(1==sscanf(arg,"end=%d",&option))
	{
		end = option;
		printf("END AT %d!\n",start);
		return;
	}

	if(1==sscanf(arg,"files=%s",buf))
	{
		source = buf;
		printf("loading data from %s!\n", source.c_str());
		return;
	}

	if(1==sscanf(arg,"calib=%s",buf))
	{
		calib = buf;
		printf("loading calibration from %s!\n", calib.c_str());
		return;
	}

	if(1==sscanf(arg,"vignette=%s",buf))
	{
		vignette = buf;
		printf("loading vignette from %s!\n", vignette.c_str());
		return;
	}

	if(1==sscanf(arg,"gamma=%s",buf))
	{
		gammaCalib = buf;
		printf("loading gammaCalib from %s!\n", gammaCalib.c_str());
		return;
	}

	if(1==sscanf(arg,"rescale=%f",&foption))
	{
		rescale = foption;
		printf("RESCALE %f!\n", rescale);
		return;
	}

	if(1==sscanf(arg,"speed=%f",&foption))
	{
		playbackSpeed = foption;
		printf("PLAYBACK SPEED %f!\n", playbackSpeed);
		return;
	}

	if(1==sscanf(arg,"save=%d",&option))
	{
		if(option==1)
		{
			debugSaveImages = true;
			if(42==system("rm -rf images_out")) printf("system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n");
			if(42==system("mkdir images_out")) printf("system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n");
			if(42==system("rm -rf images_out")) printf("system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n");
			if(42==system("mkdir images_out")) printf("system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n");
			printf("SAVE IMAGES!\n");
		}
		return;
	}

	if(1==sscanf(arg,"mode=%d",&option))
	{

		mode = option;
		if(option==0)
		{
			printf("PHOTOMETRIC MODE WITH CALIBRATION!\n");
		}
		if(option==1)
		{
			printf("PHOTOMETRIC MODE WITHOUT CALIBRATION!\n");
			setting_photometricCalibration = 0;
			setting_affineOptModeA = 0; //-1: fix. >=0: optimize (with prior, if > 0).
			setting_affineOptModeB = 0; //-1: fix. >=0: optimize (with prior, if > 0).
		}
		if(option==2)
		{
			printf("PHOTOMETRIC MODE WITH PERFECT IMAGES!\n");
			setting_photometricCalibration = 0;
			setting_affineOptModeA = -1; //-1: fix. >=0: optimize (with prior, if > 0).
			setting_affineOptModeB = -1; //-1: fix. >=0: optimize (with prior, if > 0).
            setting_minGradHistAdd=3;
		}
		return;
	}
	
	if(1==sscanf(arg, "online=%d", &option))
	{
		onlineCam = option;
		printf("USING ONLINE CAMERA!\n");
		return;
	}

	// External depth options
	if(1==sscanf(arg, "extDepth=%s", buf))
	{
		extDepthFolder = std::string(buf);
		extDepth = true;
		std::cout << "Using external depth from: " << extDepthFolder << std::endl;
		return;
	}

	if(1==sscanf(arg, "minXDepth=%d", &extDepth_minX))
	{
		std::cout << "External depth min X: " << extDepth_minX << std::endl;
		return;
	}

	if(1==sscanf(arg, "maxXDepth=%d", &extDepth_maxX))
	{
		std::cout << "External depth max X: " << extDepth_maxX << std::endl;
		return;
	}

	if(1==sscanf(arg, "minYDepth=%d", &extDepth_minY))
	{
		std::cout << "External depth min Y: " << extDepth_minY << std::endl;
		return;
	}

	if(1==sscanf(arg, "maxYDepth=%d", &extDepth_maxY))
	{
		std::cout << "External depth max Y: " << extDepth_maxY << std::endl;
		return;
	}
	
	if(1==sscanf(arg, "scaleDepth=%d", &option))
	{
		scaleRecovFromDepth = option;
		std::cout << "SCALE RECOVERY!" << std::endl;
		return;
	}
	
	if(1==sscanf(arg, "extDepthImmature=%d", &option))
	{
		extDepthImmature = option;
		std::cout << "External depth used for immature points prior" << std::endl;
		return;
	}

	if(1==sscanf(arg, "trajLog=%d", &option))
	{
		trajLog = option;
		
		if(42==system("mkdir logs")) printf("system call returned 42 - what are the odds?. This is only here to shut up the compiler.\n");
		
		std::cout << "TRAJECTORY LOGGING!" << std::endl;
		return;
	}

	if(1==sscanf(arg, "timings=%d", &option))
	{
		printTimings = option;
		std::cout << "PRINTING TIMINGS!" << std::endl;
		return;
	}

	printf("could not parse argument \"%s\"!!!!\n", arg);
}



int main( int argc, char** argv )
{
	//setlocale(LC_ALL, "");
	for(int i=1; i<argc;i++)
		parseArgument(argv[i]);

	// hook crtl+C.
	boost::thread exThread = boost::thread(exitThread);


	ImageFolderReader* reader = new ImageFolderReader(source, calib, gammaCalib, vignette);
	reader->setGlobalCalibration();

	// Init camera
	printf("Initialisating camera... ");
	raspicam::RaspiCam_Cv cam;
	cv::Mat onlineImg;
	if(onlineCam)
	{
		cam.set(CV_CAP_PROP_FORMAT, CV_8UC1);
		cam.set(CV_CAP_PROP_FRAME_WIDTH, 512);
		cam.set(CV_CAP_PROP_FRAME_HEIGHT, 512);

		if(!cam.open())
		{
			printf("Error opening the camera!\n");
			return EXIT_FAILURE;
		}

		// Take few first frame (To stabilise exposure if auto)
		for(int k = 0; k < 5; ++k)
		{
			cam.grab();
			cam.retrieve(onlineImg);
			printf(".");

			std::this_thread::sleep_for(std::chrono::milliseconds(100));		
		}
	    	printf("\nShutter speed: %ums\n", (cam.get(CV_CAP_PROP_EXPOSURE) * 3300));
	    	printf("ISO: %d\n", (cam.get(CV_CAP_PROP_GAIN) * 800));

	}
	printf(" done.\n");

	if(setting_photometricCalibration > 0 && reader->getPhotometricGamma() == 0)
	{
		printf("ERROR: dont't have photometric calibration. Need to use commandline options mode=1 or mode=2 ");
		exit(1);
	}

	// Check external depth parameters
	if((scaleRecovFromDepth || extDepthImmature) && !extDepth)
	{
		std::cout << "WARNING: You either want scale recovery from depth or external depth use in immature points tracking but \"extDepth\" is not set. Setting these to false..." << std::endl;
		scaleRecovFromDepth = extDepthImmature = false;
	}
	else if(extDepth && !(scaleRecovFromDepth || extDepthImmature))
	{
		std::cout << "WARNING: You set external depth but neither \"scaleDepth\" nor \"depthImmature\" are set. As extDepth won't be used, it is set to false." << std::endl;
		extDepth = false;
	}

	// Check scale estimation (Check for IMU scale eventually (not yet))
	if(scaleRecovFromDepth)
		scaleEstimation = true;

	int lstart=start;
	int lend = end;
	int linc = 1;
	if(reverse && !onlineCam)
	{
		printf("REVERSE!!!!");
		lstart=end-1;
		if(lstart >= reader->getNumImages())
			lstart = reader->getNumImages()-1;
		lend = start;
		linc = -1;
	}
	

	FullSystem* fullSystem = new FullSystem();
	fullSystem->setGammaFunction(reader->getPhotometricGamma());
	fullSystem->linearizeOperation = (playbackSpeed==0);


	IOWrap::InputDepthWrapper inputDepthWrap(wG[0], hG[0], extDepthFolder);
	
    	IOWrap::PangolinDSOViewer* viewer = 0;
	if(!disableAllDisplay)
    	{
        	viewer = new IOWrap::PangolinDSOViewer(wG[0],hG[0], false);
        	fullSystem->outputWrapper.push_back(viewer);
    	}

	if(useSampleOutput)
        	fullSystem->outputWrapper.push_back(new IOWrap::SampleOutputWrapper());

	// Init log files if needed
	if(trajLog)
		fullSystem->initTrajLogs();

    // to make MacOS happy: run this in dedicated thread -- and use this one to run the GUI.
    std::thread runthread([&]() {
        std::vector<int> idsToPlay;
        std::vector<double> timesToPlayAt;
        
	if(!onlineCam)
	{
		for(int i=lstart;i>= 0 && i< reader->getNumImages() && linc*i < linc*lend;i+=linc)
        	{
        	    idsToPlay.push_back(i);
        	    if(timesToPlayAt.size() == 0)
        	    {
        	        timesToPlayAt.push_back((double)0);
        	    }
        	    else
        	    {
        	        double tsThis = reader->getTimestamp(idsToPlay[idsToPlay.size()-1]);
        	        double tsPrev = reader->getTimestamp(idsToPlay[idsToPlay.size()-2]);
        	        timesToPlayAt.push_back(timesToPlayAt.back() +  fabs(tsThis-tsPrev)/playbackSpeed);
        	    }
        	}
	}

	// Checking extDepth limit values
	if(extDepth_minX == -1) extDepth_minX = 1;
	if(extDepth_minY == -1) extDepth_minY = 1;
	if(extDepth_maxX == -1) extDepth_maxX = wG[0];
	if(extDepth_maxY == -1) extDepth_maxY = hG[0];

	Eigen::Vector4f extDepthLimits(-1, -1, -1, -1);
	
	if(extDepth)
		extDepthLimits = Eigen::Vector4f(extDepth_minX, extDepth_maxX,
						extDepth_minY, extDepth_maxY);

	std::cout << "External depth limits: minX: " << extDepthLimits(0) << " maxX: " << extDepthLimits(1) << " minY: " << extDepthLimits(2) << " maxY: " << extDepthLimits(3) << std::endl;


        std::vector<ImageAndExposure*> preloadedImages;
        if(preload && !onlineCam)
        {
            printf("LOADING ALL IMAGES!\n");
            for(int imgIdx=0; imgIdx<(int)idsToPlay.size(); imgIdx++)
            {
                int i = idsToPlay[imgIdx];
                preloadedImages.push_back(reader->getImage(i));
            }
        }

        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        clock_t started = clock(), loopBegin, loopEnd;
        double sInitializerOffset=0, timeElapsedLoopMs = -1;
	int imgIdx = 0, idsToPlayLength = onlineCam ? -1 : ((int) idsToPlay.size());
	
        while((imgIdx < idsToPlayLength && !onlineCam) || onlineCam)
        {
		loopBegin = clock();
		if(!fullSystem->initialized)	// if not initialized: reset start time.
		{
			gettimeofday(&tv_start, NULL);
			started = clock();
			if(!onlineCam)
			sInitializerOffset = timesToPlayAt[imgIdx];
			else
			sInitializerOffset = 0;
		}

		int i;
		if(onlineCam)
		i = 0;
		else
		i = idsToPlay[imgIdx];


		ImageAndExposure* img;
		if(preload)
			img = preloadedImages[imgIdx];
		else if(onlineCam)
		{
			cam.grab();
			cam.retrieve(onlineImg);
			reader->setCurrentImg(&onlineImg);
			img = reader->getImage(i); // The parameter won't be taken into account (because onlineCam = true)
		}	
		else
		img = reader->getImage(i);

		// Load external depth file into wrapper 
		bool loadedDepthFile = false;
		if(extDepth)
		{
			loadedDepthFile = inputDepthWrap.loadDepthFile(i);
			if(!loadedDepthFile)
				std::cout << "Error: inputDepth is empty." << std::endl;
		}

		bool skipFrame=false;
		if(playbackSpeed!=0 && !onlineCam)
		{
			struct timeval tv_now; gettimeofday(&tv_now, NULL);
			double sSinceStart = sInitializerOffset + ((tv_now.tv_sec-tv_start.tv_sec) + (tv_now.tv_usec-tv_start.tv_usec)/(1000.0f*1000.0f));

			if(sSinceStart < timesToPlayAt[imgIdx])
				usleep((int)((timesToPlayAt[imgIdx]-sSinceStart)*1000*1000));
			else if(sSinceStart > timesToPlayAt[imgIdx]+0.5+0.1*(imgIdx%2))
			{
				printf("SKIPFRAME %d (play at %f, now it is %f)!\n", imgIdx, timesToPlayAt[imgIdx], sSinceStart);
				skipFrame=true;
			}
		}

		// IMU measurement
		Eigen::Vector4f attitudeReadings(1.0, 0.0, 0.0, 0.0);

		// clock_t startFrame = clock();
		if(!skipFrame)
		{
			if(extDepth && loadedDepthFile)
				fullSystem->addActiveFrame(img, inputDepthWrap.getDepths(), extDepthLimits, attitudeReadings, i);
			else
			{
				std::vector<float> emptyVec;
				fullSystem->addActiveFrame(img, emptyVec, extDepthLimits, attitudeReadings, i);
			}
		}

		// clock_t endFrame = clock();
		// std::cout << "Time activating frame: " << (1000.0f * (endFrame - startFrame) / (float) (CLOCKS_PER_SEC)) << std::endl;

		delete img;

		if(fullSystem->initFailed || setting_fullResetRequested)
		{
			if(imgIdx < 250 || setting_fullResetRequested)
			{
				printf("RESETTING!\n");

				std::vector<IOWrap::Output3DWrapper*> wraps = fullSystem->outputWrapper;
				delete fullSystem;

				for(IOWrap::Output3DWrapper* ow : wraps) ow->reset();

				fullSystem = new FullSystem();
				fullSystem->setGammaFunction(reader->getPhotometricGamma());
				fullSystem->linearizeOperation = (playbackSpeed==0);


				fullSystem->outputWrapper = wraps;

				setting_fullResetRequested=false;
			}
		}

		if(fullSystem->isLost)
		{
			printf("LOST!!\n");
			break;
		}
	
		// Output timings
		loopEnd = clock();
		timeElapsedLoopMs = 1000.0 * (loopEnd - loopBegin) / ((float) CLOCKS_PER_SEC);
		
		std::cout << "It. time: " << timeElapsedLoopMs << "ms." << std::endl;

		// Output states
		//std::cout << "+";
		//std::cout << "State (pos, quat): ";
		//fullSystem->printPose();
		//std::cout << std::endl;

		if(trajLog)
		{
			fullSystem->logLastPose(i);
			fullSystem->logLastDepth(i);
		}


		imgIdx++;
        }
        
	fullSystem->blockUntilMappingIsFinished();
        clock_t ended = clock();
        struct timeval tv_end;
        gettimeofday(&tv_end, NULL);


        fullSystem->printResult("result.txt");


        int numFramesProcessed = onlineCam ? imgIdx : abs(idsToPlay[0]-idsToPlay.back());
        double numSecondsProcessed = onlineCam ? -1 : fabs(reader->getTimestamp(idsToPlay[0])-reader->getTimestamp(idsToPlay.back()));
        double MilliSecondsTakenSingle = 1000.0f*(ended-started)/(float)(CLOCKS_PER_SEC);
        double MilliSecondsTakenMT = sInitializerOffset + ((tv_end.tv_sec-tv_start.tv_sec)*1000.0f + (tv_end.tv_usec-tv_start.tv_usec)/1000.0f);
        printf("\n======================"
                "\n%d Frames (%.1f fps)"
                "\n%.2fms per frame (single core); "
                "\n%.2fms per frame (multi core); "
                "\n%.3fx (single core); "
                "\n%.3fx (multi core); "
                "\n======================\n\n",
                numFramesProcessed, numFramesProcessed/numSecondsProcessed,
                MilliSecondsTakenSingle/numFramesProcessed,
                MilliSecondsTakenMT / (float)numFramesProcessed,
                1000 / (MilliSecondsTakenSingle/numSecondsProcessed),
                1000 / (MilliSecondsTakenMT / numSecondsProcessed));
        //fullSystem->printFrameLifetimes();
        if(setting_logStuff)
        {
            std::ofstream tmlog;
            tmlog.open("logs/time.txt", std::ios::trunc | std::ios::out);
            tmlog << 1000.0f*(ended-started)/(float)(CLOCKS_PER_SEC*reader->getNumImages()) << " "
                  << ((tv_end.tv_sec-tv_start.tv_sec)*1000.0f + (tv_end.tv_usec-tv_start.tv_usec)/1000.0f) / (float)reader->getNumImages() << "\n";
            tmlog.flush();
            tmlog.close();
        }

    });


    	if(viewer != 0)
        	viewer->run();

    	runthread.join();

	for(IOWrap::Output3DWrapper* ow : fullSystem->outputWrapper)
	{
		ow->join();
		delete ow;
	}



	printf("DELETE FULLSYSTEM!\n");
	delete fullSystem;

	printf("DELETE READER!\n");
	delete reader;

	printf("EXIT NOW!\n");
	return 0;
}
