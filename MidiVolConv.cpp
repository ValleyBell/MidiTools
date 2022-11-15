#define _USE_MATH_DEFINES
#include <iostream>
#include <fstream>
#include <string>
#include <ctype.h>	// for tolower()
#include <string.h>	// for stricmp
#include <math.h>

#include <stdtype.h>
#include "MidiLib.hpp"

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif
#ifndef M_LN2
#define M_LN2	0.693147180559945309417
#endif
#ifndef M_LN10
#define M_LN10	2.30258509299404568402
#endif

#ifdef _MSC_VER
#define stricmp		_stricmp
#else
#define stricmp		strcasecmp
#endif

// Function Prototypes
static UINT8 GetVolAlgoName(const char* algoName);
void MidiVolConv(void);
static double GetDBVol(UINT8 inVol);
static UINT8 GetMIDIVol(double dbVol, bool noVol0);
static UINT8 VolConv(UINT8 inVol, bool noVol0);


struct VOLALGO_LIST
{
	UINT8 id;
	const char* name;
};

#define VOLALGO_GM		0x00
#define VOLALGO_LIN		0x01
#define VOLALGO_FM		0x02
#define VOLALGO_PSG_2DB	0x03
#define VOLALGO_PSG_3DB	0x04
#define VOLALGO_WINFM	0x05

static const VOLALGO_LIST VolAlgoList[] =
{
	{VOLALGO_GM, "GM"},
	{VOLALGO_LIN, "Lin"},
	{VOLALGO_FM, "FM"},
	{VOLALGO_PSG_2DB, "PSG2"},
	{VOLALGO_PSG_3DB, "PSG3"},
	{VOLALGO_WINFM, "WinFM"},
	{0x00, NULL}
};

#define VOLEVT_VELOCITY		0x01
#define VOLEVT_VOLUME		0x02
#define VOLEVT_EXPRESSION	0x04
#define VOLEVT_ALL			(VOLEVT_VELOCITY | VOLEVT_VOLUME | VOLEVT_EXPRESSION)

static UINT8 VOLEVT_MASK;
static UINT16 CHANNEL_MASK;
static UINT8 INVOL_ALGO;
static UINT8 OUTVOL_ALGO;
static double VOL_GAIN;
MidiFile CMidi;

int main(int argc, char* argv[])
{
	int argbase;
	
	std::cout << "MIDI Volume Converter\n";
	std::cout << "---------------------\n";
	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " [options] input.mid output.mid\n";
		std::cout << "Options:\n";
		std::cout << "    -s Algo - set volume algorithm (source/input) (default: -s GM)\n";
		std::cout << "    -d Algo - set volume algorithm (destination/output) (default: -d GM)\n";
	//	std::cout << "    -e evts - convert specified events only (default: -e Vel,Vol,Exp)\n";
	//	std::cout << "              Vel = Note Velocity, Vol = Volume Ctrl, Exp = Expression Ctrl\n";
		std::cout << "    -g gain - change volume by gain (in db, default: 0)\n";
		std::cout << "Algorithms:\n";
		std::cout << "    GM    - General MIDI algorithm\n";
		std::cout << "    Lin   - linear volume (127 = max, 64 = half volume)\n";
		std::cout << "    FM    - Yamaha FM (0.75 db steps)\n";
		std::cout << "    PSG2  - SN76489 PSG, 2 db per 8 steps\n";
		std::cout << "    PSG3  - AY8910 PSG, 3 db per 8 steps\n";
		std::cout << "    WinFM - Windows FM MIDI algorithm\n";
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	argbase = 1;
	VOLEVT_MASK = VOLEVT_ALL;
	CHANNEL_MASK = 0xFFFF;	// all 16 channels active
	INVOL_ALGO = VOLALGO_GM;
	OUTVOL_ALGO = VOLALGO_GM;
	VOL_GAIN = 0.0;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		char optChr = tolower(argv[argbase][1]);
		
		if (optChr == 's' || optChr == 'd')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			UINT8 algoID = GetVolAlgoName(argv[argbase]);
			if (algoID == 0xFF)
			{
				std::cout << "Unknown Algorithm!\n";
				break;
			}
			if (optChr == 's')
				INVOL_ALGO = algoID;
			else if (optChr == 'd')
				OUTVOL_ALGO = algoID;
		}
		else if (optChr == 'e')
		{
			argbase ++;
			if (argbase >= argc)
				break;
		}
		else if (optChr == 'g')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			VOL_GAIN = strtod(argv[argbase], NULL);
		}
		else
		{
			break;
		}
		argbase ++;
	}
	if (argc < argbase + 2)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	UINT8 retVal;
	
	std::cout << "Opening ...\n";
	retVal = CMidi.LoadFile(argv[argbase + 0]);
	if (retVal)
	{
		std::cout << "Error opening file!\n";
		std::cout << "Errorcode: " << retVal;
		return retVal;
	}
	
	MidiVolConv();
	
	std::cout << "Saving ...\n";
	retVal = CMidi.SaveFile(argv[argbase + 1]);
	if (retVal)
	{
		std::cout << "Error saving file!\n";
		std::cout << "Errorcode: " << retVal;
		return retVal;
	}
	
	std::cout << "Cleaning ...\n";
	CMidi.ClearAll();
	std::cout << "Done.\n";
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

static UINT8 GetVolAlgoName(const char* algoName)
{
	const VOLALGO_LIST* tempAlgo;
	
	for (tempAlgo = VolAlgoList; tempAlgo->name != NULL; tempAlgo ++)
	{
		if (! stricmp(algoName, tempAlgo->name))
			return tempAlgo->id;
	}
	return 0xFF;
}

void MidiVolConv(void)
{
	UINT16 trkCnt;
	UINT16 curTrk;
	
	trkCnt = CMidi.GetTrackCount();
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		MidiTrack* midiTrk = CMidi.GetTrack(curTrk);
		midevt_iterator evtIt;
		
		for (evtIt = midiTrk->GetEventBegin(); evtIt != midiTrk->GetEventEnd(); ++evtIt)
		{
			UINT8 evtChn = evtIt->evtType & 0x0F;
			switch(evtIt->evtType & 0xF0)
			{
			case 0x80:
			case 0x90:
				if (! (CHANNEL_MASK & (1 << evtChn)))
					break;
				if ((VOLEVT_MASK & VOLEVT_VELOCITY) && evtIt->evtValB > 0)
					evtIt->evtValB = VolConv(evtIt->evtValB, true);
				break;
			case 0xB0:
				if (! (CHANNEL_MASK & (1 << evtChn)))
					break;
				switch(evtIt->evtValA)
				{
				case 0x07:
					if ((VOLEVT_MASK & VOLEVT_VOLUME) && evtIt->evtValB > 0)
						evtIt->evtValB = VolConv(evtIt->evtValB, false);
					break;
				case 0x0B:
					if ((VOLEVT_MASK & VOLEVT_EXPRESSION) && evtIt->evtValB > 0)
						evtIt->evtValB = VolConv(evtIt->evtValB, false);
					break;
				}
				break;
			}
		}	// end while(evtIt)
	}
	
	return;
}

static double GetDBVol(UINT8 inVol)
{
	switch(INVOL_ALGO)
	{
	case VOLALGO_GM:	// General MIDI scale
		return 40.0 * log(inVol / 127.0) / M_LN10;
	case VOLALGO_LIN:	// linear scale
		return 6.0 * log(inVol / 127.0) / M_LN2;
	case VOLALGO_FM:	// FM OPx scale (0.75 db per step)
		return (inVol - 0x7F) / 8.0 * 6.0;
	case VOLALGO_PSG_2DB:	// PSG scale (8 values, one step, 2 db)
		inVol /= 0x08;	// truncate low 3 bits
		return (inVol - 0x0F) * 2.0;
	case VOLALGO_PSG_3DB:	// PSG scale (8 values, one step, 3 db)
		inVol /= 0x08;	// truncate low 3 bits
		return (inVol - 0x0F) * 3.0;
	case VOLALGO_WINFM:
		{
			double volPerc = inVol / 127.0;
			double a2 = sin(volPerc * (M_PI / 2.0));
			double a3 = sqrt(a2) * 0.9;
			double oplTL = 0x3F * (1.0 - a3);
			return oplTL / 8.0 * -6.0 + 4.725;	// add 4.725 to make up for the *0.9 above
		}
	}
	
	return 0.0;
}

static UINT8 GetMIDIVol(double dbVol, bool noVol0)
{
	double volVal;
	UINT8 midVol;
	
	switch(OUTVOL_ALGO)
	{
	case VOLALGO_GM:	// General MIDI scale
		volVal = pow(10.0, dbVol / 40.0);
		break;
	case VOLALGO_LIN:	// linear scale
		volVal = pow(2.0, dbVol / 6.0);
		break;
	case VOLALGO_FM:	// FM OPx scale (0.75 db per step)
		volVal = (dbVol / 6.0 * 8.0 + 127) / 127.0;
		break;
	case VOLALGO_PSG_2DB:	// PSG scale (8 values, one step, 2 db)
		volVal = (dbVol / 2.0 * 8.0 + 120) / 120.0;
		break;
	case VOLALGO_PSG_3DB:	// PSG scale (8 values, one step, 3 db)
		volVal = (dbVol / 3.0 * 8.0 + 120) / 120.0;
		break;
	case VOLALGO_WINFM:
		{
			double oplTL = (dbVol - 4.725) / -6.0 * 8.0;
			double a3 = 1.0 - oplTL / 0x3F;
			double a2 = pow(a3 / 0.9, 2);
			volVal = asin(a2) / (M_PI / 2.0);
			break;
		}
	default:
		volVal = 1.0;
		break;
	}
	if (volVal < 0.0)
		volVal = 0.0;
	else if (volVal > 1.0)
		volVal = 1.0;
	
	midVol = (UINT8)(volVal * 0x7F + 0.5);
	if (noVol0 && midVol == 0)
		midVol = 1;
	return midVol;
}

static UINT8 VolConv(UINT8 inVol, bool noVol0)
{
	return GetMIDIVol(GetDBVol(inVol) + VOL_GAIN, noVol0);
}
