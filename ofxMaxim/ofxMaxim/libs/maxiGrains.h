

#ifndef _MAXI_GRAINS_H
#define _MAXI_GRAINS_H
#include "maximilian.h"

#if defined(__APPLE_CC__)
#include "accelerate/accelerate.h"
//Mac users can uncommment the line below to use Apple's accelerate framework for calculating grains. This gives ~300% speed improvement and better sound quality, but doesn't work well on all machines.
//#define MAXIGRAINFAST  
#endif

#include <list>

typedef unsigned long ulong;

//window functions from http://en.wikipedia.org/wiki/Window_function#High-_and_moderate-resolution_windows

struct hannWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return 0.5 * (1.0 - cos((2.0 * PI * windowPos) / (windowLength - 1)));
	}
};

//this window can produce clicks
struct hammingWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return 0.54 - (0.46 * cos((2.0 * PI * windowPos) / (windowLength - 1)));
	}
};


struct cosineWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return sin((PI * windowPos) / (windowLength - 1));
	}
};

struct rectWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return 1;
	}
};

struct triangleWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return (2.0 / (windowLength-1.0)) * (((windowLength-1.0)/2.0) - fabs(windowPos - ((windowLength-1.0)/2.0)));
	}
};

struct triangleNZWinFunctor {
	//non zero end points
	inline double operator()(ulong windowLength, ulong windowPos) {
		return (2.0 / windowLength) * ((windowLength/2.0) - fabs(windowPos - ((windowLength-1.0)/2.0)));
	}
};

struct blackmanHarrisWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return 0.35875 - 
		(0.48829 * cos((2 * PI * windowPos) / (windowLength-1))) +
		(0.14128 * cos((4 * PI * windowPos) / (windowLength-1))) +
		(0.01168 * cos((6 * PI * windowPos) / (windowLength-1)));
	}
};

struct blackmanNutallWinFunctor {
	inline double operator()(ulong windowLength, ulong windowPos) {
		return 0.3635819 - 
		(0.4891775 * cos((2 * PI * windowPos) / (windowLength-1))) +
		(0.1365995 * cos((4 * PI * windowPos) / (windowLength-1))) +
		(0.0106411 * cos((6 * PI * windowPos) / (windowLength-1)));
	}
};

struct gaussianWinFunctor {
    double gausDivisor;
    gaussianWinFunctor() {
        init(0.3);
    }
    gaussianWinFunctor(double kurtosis) {
        init(kurtosis);
    }
    void init(double kurtosis) {
        gausDivisor = (-2.0 * kurtosis * kurtosis);
    }
	inline double operator()(ulong windowLength, ulong windowPos) {
        double phase = ((windowPos / (double) windowLength) - 0.5) * 2.0;
        return exp((phase * phase) / gausDivisor);
	}
};


template<typename F>
class maxiGrainWindowCache {
public:
	unsigned int cacheSize; 
	
	maxiGrainWindowCache() {
		cacheSize = maxiSettings::sampleRate / 2.0; //allocate mem for up to 500ms grains
		cache = (double**)malloc(cacheSize * sizeof(double*));
		for(int i=0; i < cacheSize; i++) {
			cache[i] = NULL;
		}
	}
	
	~maxiGrainWindowCache() {
		for(int i=0; i < cacheSize; i++) {
			if(NULL != cache[i]) {
				delete[] cache[i];
			}
		}
	}
	
	double* getWindow(const unsigned int length) {
		if (NULL == cache[length]) {
			cache[length] = (double*)malloc(length * sizeof(double));
			for(int i=0; i < length; i++) {
				cache[length][i] = F()(length, i);
			}
		}
		return cache[length];
	}
	
private:
	double** cache;
	
};

class maxiGrainBase {
public:
	virtual double play() = 0;
	bool finished;
};

template<typename F>
class maxiGrain : public maxiGrainBase {
public:
	maxiSample *sample;
	double pos;
	double dur;
	ulong sampleStartPos;
	ulong sampleIdx;
	ulong sampleDur;
	ulong sampleEndPos;
	double freq;
	double speed;
	//	F winFunc;
	double inc;
	double frequency;
	double* window;
#if defined(__APPLE_CC__) && defined(MAXIGRAINFAST)
	double* grainSamples;
#endif
	/*
	 position between 0.0 and 1.0
	 duration in seconds
	 */
	maxiGrain(maxiSample *sample, const double position, const double duration, const double speed, maxiGrainWindowCache<F> *windowCache) :sample(sample), pos(position), dur(duration), speed(speed) 
	{
		sampleStartPos = sample->length * pos;
		sampleDur = dur * (double)sample->mySampleRate;
		sampleDurMinusOne = sampleDur - 1;
		sampleIdx = 0;
		finished = 0;
		freq = 1.0 / dur;
		sampleEndPos = sampleStartPos + sampleDur;
		frequency = freq * speed;		
		if (frequency > 0) {
			pos = sampleStartPos;
		}else{
			pos = sampleEndPos;
		}
		inc = sampleDur/(maxiSettings::sampleRate/frequency);
		window = windowCache->getWindow(sampleDur);
		
#if defined(__APPLE_CC__) && defined(MAXIGRAINFAST)
		//premake the grain using fast vector functions, and quadratic interpolation
		double *sourceData = (double*)malloc(sampleDur * sizeof(double));
		short* buffer = (short *)sample->myData;
		//convert sample to double data
		vDSP_vflt16D(buffer + sampleStartPos, 1, sourceData, 1, min(sampleDur, sample->length - sampleStartPos));
		//todo: wraping code
		
		grainSamples = (double*)malloc(sampleDur * sizeof(double));
		//make list of interpolation indexes
		double* interpIndexes = (double*)malloc(sampleDur * sizeof(double));
		double interpPos = sampleStartPos;
		for(int i=0; i < sampleDur; i++) {
			interpIndexes[i] = interpPos - sampleStartPos;
			interpPos += fabs(inc);
		}
		vDSP_vqintD(sourceData, interpIndexes, 1, grainSamples, 1, sampleDur, sampleDur);
		if (frequency < 0) {
			vDSP_vrvrsD(grainSamples,1, sampleDur);
		}
		static double divFactor = 32767.0;
		vDSP_vsdivD(grainSamples, 1, &divFactor, grainSamples, 1, sampleDur);
		vDSP_vmulD(grainSamples, 1, window, 1, grainSamples, 1, sampleDur);
		delete sourceData, interpIndexes;		
#endif
	}
	
	~maxiGrain() {
#if defined(__APPLE_CC__) && defined(MAXIGRAINFAST)
		delete[] grainSamples;
#endif
	}
	
	double play() {
		double output = 0.0;
		if (!finished) {
#if defined(__APPLE_CC__) && defined(MAXIGRAINFAST)
			output = grainSamples[sampleIdx];
#else
			envValue = window[sampleIdx];
			double remainder;
			long a,b;
			short* buffer = sample->temp;
			if (frequency >0.) {
				pos += inc;
                if (pos >= sample->length) pos -= sample->length;
				long posl = static_cast<long>(pos);
				remainder = pos - posl;
				a=posl++;
				if (a>=sample->length) {
					a=posl-1;
				}
				b = posl+2;
				if (b>=sample->length) {
					b=sample->length-1;
				}
				output = (double) ((1-remainder) * buffer[a] +
								   remainder * buffer[b])/32767;//linear interpolation
			} else {
				frequency=frequency-(frequency+frequency);
				pos += inc;
                if (pos < 0) pos += sample->length;
				long posl = static_cast<long>(pos);
				remainder = pos - posl;
				if (posl-1>=0) {
					a=posl-1;
				}
				else {
					a=0;
				}
				if (posl-2>=0) {
					b=posl-2;
				}
				else {
					b=0;
				}		
				output = (double) ((-1-remainder) * buffer[a] +
								   remainder * buffer[b])/32767;//linear interpolation
			}
			output *= envValue;
#endif
		}
		sampleIdx++;
		if (sampleIdx >= sampleDur) finished = 1;
		return output;
	}
	
private:	   
	maxiGrain();	
	double envValue;
	ulong sampleDurMinusOne;
};


typedef list<maxiGrainBase*> grainList;

class maxiGrainPlayer {
public:
	grainList grains;
	maxiSample *sample;
	
	maxiGrainPlayer(maxiSample *sample) : sample(sample) {
	}
	
	void addGrain(maxiGrainBase *g) {
		grains.push_back(g);
	}
	
	double play() {
		double total = 0.0;		
		for(grainList::iterator it = grains.begin();  it != grains.end(); ++it) {
			total += (*it)->play();
			if ((*it)->finished) {
				delete(*it);
				grains.erase(it);
			}
		}
		return total;
	}
};

template<typename F>
class maxiTimestretch {
public:
	double position;
	long cycles;
	maxiSample *sample;
	maxiGrainPlayer *grainPlayer;
	maxiGrainWindowCache<F> windowCache;
	double randomOffset;
	
	maxiTimestretch(maxiSample *sample) : sample(sample) {
		position=0;
		cycles=0;
		grainPlayer = new maxiGrainPlayer(sample);
		randomOffset=0;
	}
	
	~maxiTimestretch() {
		delete grainPlayer;
	}
	
	double play(double speed, double grainLength, int overlaps, double posMod=0.0) {
		position = position + (1 * speed);
		cycles++;
		if (position > sample->length) position=0;
		if (position < 0) position = sample->length;
		double cycleLength = grainLength * maxiSettings::sampleRate  / overlaps;
		double cycleMod = fmod(cycles, cycleLength + randomOffset);
		if (0 == floor(cycleMod)) {
			//			cout << cycleMod << endl;
			speed = (speed > 0 ? 1 : -1);
			//			speed = speed - ((cycleMod / cycleLength) * 0.1);
			maxiGrain<F> *g = new maxiGrain<F>(sample, max(min(1.0,(position / sample->length) + posMod),0.0), grainLength, speed, &windowCache);			
			grainPlayer->addGrain(g);
			//			cout << grainPlayer->grains.size() << endl;
			//			randomOffset = rand() % 10;
			//			randomOffset = rand() % 10;
		}
		return grainPlayer->play();
	}
	
	double play2(double pos, double grainLength, int overlaps) {
		cycles++;
		pos *= sample->length;
		if (0 == floor(fmod(cycles, grainLength * maxiSettings::sampleRate / overlaps))) {
			maxiGrain<F> *g = new maxiGrain<F>(sample, max(min(1.0,(pos / sample->length)),0.0), grainLength, 1, &windowCache);			
			grainPlayer->addGrain(g);
		}
		return grainPlayer->play();
	}
};

//in maxiTimeStretch, the speed is either 1 or -1, and the actual speed value only affects the grain position
//in maxiPitchShift, speed is uncoupled from position and allowed to set it's value incrementally, resulting in pitchshift.
//with both high speed values and negative speed values there are some terrific artefacts! 

template<typename F>
class maxiPitchShift {
public:
	double position;
	long cycles;
	maxiSample *sample;
	maxiGrainPlayer *grainPlayer;
	maxiGrainWindowCache<F> windowCache;
	double randomOffset;
	
	maxiPitchShift(maxiSample *sample) : sample(sample) {
		position=0;
		cycles=0;
		grainPlayer = new maxiGrainPlayer(sample);
		randomOffset=0;
	}
	
	~maxiPitchShift() {
		delete grainPlayer;
	}
	
	double play(double speed, double grainLength, int overlaps, double posMod=0.0) {
		position = position + 1;
		cycles++;
		if (position > sample->length) position=0;
		if (position < 0) position = sample->length;
		double cycleLength = grainLength * maxiSettings::sampleRate  / overlaps;
		double cycleMod = fmod(cycles, cycleLength + randomOffset);
		if (0 == floor(cycleMod)) {
			//			cout << cycleMod << endl;
			//speed = (speed > 0 ? 1 : -1);
			speed = speed - ((cycleMod / cycleLength) * 0.1);
			maxiGrain<F> *g = new maxiGrain<F>(sample, max(min(1.0,(position / sample->length) + posMod),0.0), grainLength, speed, &windowCache);			
			grainPlayer->addGrain(g);
			//			cout << grainPlayer->grains.size() << endl;
			//			randomOffset = rand() % 10;
			//			randomOffset = rand() % 10;
		}
		return grainPlayer->play();
	}
	
	double play2(double pos, double grainLength, int overlaps) {
		cycles++;
		pos *= sample->length;
		if (0 == floor(fmod(cycles, grainLength * maxiSettings::sampleRate / overlaps))) {
			maxiGrain<F> *g = new maxiGrain<F>(sample, max(min(1.0,(pos / sample->length)),0.0), grainLength, 1, &windowCache);			
			grainPlayer->addGrain(g);
		}
		return grainPlayer->play();
	}
};

//and here's maxiPitchStretch. Args to the play function are basically speed for 'pitch' and rate for playback rate.
//the rest is the same.

template<typename F>
class maxiPitchStretch {
public:
	double position;
	long cycles;
	maxiSample *sample;
	maxiGrainPlayer *grainPlayer;
	maxiGrainWindowCache<F> windowCache;
	double randomOffset;
	
	maxiPitchStretch(maxiSample *sample) : sample(sample) {
		position=0;
		cycles=0;
		grainPlayer = new maxiGrainPlayer(sample);
		randomOffset=0;
	}
	
	~maxiPitchStretch() {
		delete grainPlayer;
	}
	
	double play(double speed, double rate, double grainLength, int overlaps, double posMod=0.0) {
		position = position + (1 * rate);
		cycles++;
		if (position > sample->length) position=0;
		if (position < 0) position = sample->length;
		double cycleLength = grainLength * maxiSettings::sampleRate  / overlaps;
		double cycleMod = fmod(cycles, cycleLength + randomOffset);
		if (0 == floor(cycleMod)) {
			//			cout << cycleMod << endl;
			//speed = (speed > 0 ? 1 : -1);
			speed = speed - ((cycleMod / cycleLength) * 0.1);
			maxiGrain<F> *g = new maxiGrain<F>(sample, max(min(1.0,(position / sample->length) + posMod),0.0), grainLength, speed, &windowCache);			
			grainPlayer->addGrain(g);
			//			cout << grainPlayer->grains.size() << endl;
			//			randomOffset = rand() % 10;
			//			randomOffset = rand() % 10;
		}
		return grainPlayer->play();
	}
	
	double play2(double pos, double grainLength, int overlaps) {
		cycles++;
		pos *= sample->length;
		if (0 == floor(fmod(cycles, grainLength * maxiSettings::sampleRate / overlaps))) {
			maxiGrain<F> *g = new maxiGrain<F>(sample, max(min(1.0,(pos / sample->length)),0.0), grainLength, 1, &windowCache);			
			grainPlayer->addGrain(g);
		}
		return grainPlayer->play();
	}
};

#endif