// Main File
#include <iostream>
#include <fstream>
#include <string>
#include <math.h>
#include <vector>
#include <list>
#include "MidiLib.hpp"

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


struct NoteList
{
	UINT8 note;
	UINT8 chn;
};
struct TrackInfo
{
	MidiTrack* midTrk;
	
	// split by note
	UINT8 notePlaying[0x10];	// stores Note Height of currently playing note
	UINT32 noteTick[0x10];		// for event sorting
	
	// note cache for "split by instrument"
	UINT8 chnIns[0x10];	// stores Note Height of currently playing note
	std::list<NoteList> notes;
};
struct TrackSplit
{
	std::list<TrackInfo> trkList;
};

typedef std::list<TrackInfo>::iterator trkinf_iterator;


enum SPLIT_MODES
{
	SPLT_BY_CHN = 0x00,
	SPLT_BY_NOTE = 0x01,
	SPLT_BY_INS = 0x02,
	SPLT_BY_VOL = 0x03,
};

// Function Prototypes
// split by note
static void MoveNoteOn(std::list<TrackInfo>& trkLst, midevt_iterator midEvt);
static void MoveNoteOff(std::list<TrackInfo>& trkLst, midevt_iterator midEvt);
static void TrkSplit_Note(TrackSplit& trkSplt);
// split by instrument
static trkinf_iterator GetInstrumentTrack(std::list<TrackInfo>& trkLst, const MidiEvent& midiEvt);
static void TrkSplit_Instrument(TrackSplit& trkSplt);
// General
static UINT8 CountDigits(UINT32 value);
static void ModifyTrackNames(std::list<TrackInfo>& trkLst, UINT16 midiTrkID);
static void AddNoteToList(TrackInfo& trkInf, const MidiEvent& midEvt);
static trkinf_iterator RemoveNoteFromList(std::list<TrackInfo>& trkLst, midevt_iterator midEvt);
UINT8 SplitMidiTracks(UINT8 spltMode);


MidiFile CMidi;

int main(int argc, char* argv[])
{
	printf("Midi Splitter\n");
	printf("-------------\n");
	if (argc < 3)
	{
		printf("Usage: %s method input.mid output.mid\n", argv[0]);
		printf("Methods:\n");
		printf("    note - split chords\n");
		printf("    ins  - split by instrument/patch\n");
		//printf("    vol  - split by volume\n");
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	UINT8 retVal;
	UINT8 spltMode;
	
	if (! stricmp(argv[1], "Note"))
		spltMode = SPLT_BY_NOTE;
	else if (! stricmp(argv[1], "Ins"))
		spltMode = SPLT_BY_INS;
	else if (false && ! stricmp(argv[1], "Vol"))
		spltMode = SPLT_BY_VOL;
	else
		spltMode = 0xFF;
	
	if (spltMode == 0xFF)
	{
		std::cout << "Invalid method!\n";
		return 0;
	}
	
	std::cout << "Opening ...\n";
	retVal = CMidi.LoadFile(argv[2]);
	if (retVal)
	{
		std::cout << "Error opening file!\n";
		std::cout << "Errorcode: " << retVal;
		return retVal;
	}
	
	std::cout << "Splitting ...\n";
	SplitMidiTracks(spltMode);
	
	std::cout << "Saving ...\n";
	retVal = CMidi.SaveFile(argv[3]);
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

// --- Functions for "Split by Note" ---
static void MoveNoteOn(std::list<TrackInfo>& trkLst, midevt_iterator midEvt)
{
	trkinf_iterator trkIt;
	trkinf_iterator curTrk;
	UINT8 midChn;
	
	midChn = midEvt->evtType & 0x0F;
	for (trkIt = trkLst.begin(); trkIt != trkLst.end(); ++trkIt)
	{
		// find a free Track
		if (trkIt->notePlaying[midChn] == 0xFF)
		{
			// note the current Note
			trkIt->notePlaying[midChn] = midEvt->evtValA;
			trkIt->noteTick[midChn] = midEvt->tick;
			// current Track != first Track?
			if (trkIt != trkLst.begin())
			{
				// move Note to current Track
				trkIt->midTrk->AppendEvent(*midEvt);
				trkLst.begin()->midTrk->RemoveEvent(midEvt);
			}
			return;
		}
	}
	
	// make new track and initialize notePlaying array
	trkLst.push_back(TrackInfo());
	curTrk = trkLst.end();
	--curTrk;
	
	curTrk->midTrk = new MidiTrack;
	
	// make notes of the Note and move it to the current Track
	curTrk->notePlaying[midChn] = midEvt->evtValA;
	curTrk->noteTick[midChn] = midEvt->tick;
	curTrk->midTrk->AppendEvent(*midEvt);
	trkLst.begin()->midTrk->RemoveEvent(midEvt);
	
	return;
}

static void MoveNoteOff(std::list<TrackInfo>& trkLst, midevt_iterator midEvt)
{
	trkinf_iterator trkIt;
	trkinf_iterator noteTrk;
	UINT8 midChn;
	
	midChn = midEvt->evtType & 0x0F;
	noteTrk = trkLst.end();
	for (trkIt = trkLst.begin(); trkIt != trkLst.end(); ++trkIt)
	{
		// find Track where the Note is playing
		if (trkIt->notePlaying[midChn] == midEvt->evtValA)
		{
			if (noteTrk == trkLst.end())
				noteTrk = trkIt;
			else if (noteTrk->noteTick[midChn] < midEvt->tick)
				noteTrk = trkIt;
		}
	}
	// this point is reachable with noteTrk == end(), when there's a Note Off without a Note On
	if (noteTrk == trkLst.end())
		return;
	
	// current Track != first Track?
	if (noteTrk != trkLst.begin())
	{
		// move Note to current Track
		noteTrk->midTrk->AppendEvent(*midEvt);
		trkLst.begin()->midTrk->RemoveEvent(midEvt);
	}
	// set 'no Note playing'
	noteTrk->notePlaying[midChn] = 0xFF;
	
	return;
}

static void TrkSplit_Note(TrackSplit& trkSplt)
{
	trkinf_iterator trk1Inf;
	MidiTrack* midTrk;
	midevt_iterator evtIt;
	UINT8 curChn;
	
	trk1Inf = trkSplt.trkList.begin();
	midTrk = trk1Inf->midTrk;
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		trk1Inf->notePlaying[curChn] = 0xFF;
	
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); )
	{
		midevt_iterator curEvt = evtIt;
		++evtIt;	// we may change the track of curEvt
		
		switch(curEvt->evtType & 0xF0)
		{
		case 0x80:
		case 0x90:
			if ((curEvt->evtType & 0xF0) == 0x90 && curEvt->evtValB)
				MoveNoteOn(trkSplt.trkList, curEvt);
			else
				MoveNoteOff(trkSplt.trkList, curEvt);
			break;
		}	// end switch(curEvt->Event & 0xF0)
	}	// end for (evtIt)
	
	return;
}

// --- Functions for "Split by Instrument" ---
static trkinf_iterator GetInstrumentTrack(std::list<TrackInfo>& trkLst, const MidiEvent& midiEvt)
{
	trkinf_iterator curTrk;
	UINT8 midChn;
	
	midChn = midiEvt.evtType & 0x0F;
	if (trkLst.begin()->chnIns[midChn] == 0xFF)
		return trkLst.begin();
	
	for (curTrk = trkLst.begin(); curTrk != trkLst.end(); ++curTrk)
	{
		// find a Track that uses the current instrument
		if (curTrk->chnIns[midChn] == midiEvt.evtValA)
			return curTrk;
	}
	
	// make new track and initialize chnIns array
	trkLst.push_back(TrackInfo());
	curTrk = trkLst.end();
	--curTrk;
	
	curTrk->midTrk = new MidiTrack;
	for (midChn = 0x00; midChn < 0x10; midChn ++)
		curTrk->chnIns[midChn] = 0xFF;
	
	return curTrk;
}

static void TrkSplit_Instrument(TrackSplit& trkSplt)
{
	trkinf_iterator trkInfFrom;
	trkinf_iterator trkInfTo;
	MidiTrack* midTrk;
	midevt_iterator evtIt;
	UINT8 curChn;
	bool skipMove;
	
	trkInfFrom = trkSplt.trkList.begin();
	midTrk = trkInfFrom->midTrk;
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		trkInfFrom->chnIns[curChn] = 0xFF;
	trkInfFrom->notes.clear();
	
	trkInfTo = trkInfFrom;
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd();)
	{
		midevt_iterator curEvt = evtIt;
		++evtIt;	// we may change the track of curEvt
		
		skipMove = (curEvt->evtType >= 0xF0);
		switch(curEvt->evtType & 0xF0)
		{
		case 0x80:
		case 0x90:
			if ((curEvt->evtType & 0xF0) == 0x90 && curEvt->evtValB)
			{
				curChn = curEvt->evtType & 0x0F;
				if (trkInfFrom->chnIns[curChn] == 0xFF)
					trkInfFrom->chnIns[curChn] = 0x00;
				
				AddNoteToList(*trkInfTo, *curEvt);
			}
			else
			{
				trkinf_iterator noteOnTrk = RemoveNoteFromList(trkSplt.trkList, curEvt);
				if (noteOnTrk != trkSplt.trkList.end())
				{
					if (noteOnTrk != trkInfFrom)
					{
						// move NoteOff event to track of NoteOn event
						noteOnTrk->midTrk->AppendEvent(*curEvt);
						trkInfFrom->midTrk->RemoveEvent(curEvt);
					}
					skipMove = true;
				}
			}
			break;
		case 0xC0:
			trkInfTo = GetInstrumentTrack(trkSplt.trkList, *curEvt);
			
			curChn = curEvt->evtType & 0x0F;
			trkInfTo->chnIns[curChn] = curEvt->evtValA;
			break;
		}	// end switch(curEvt->evtType & 0xF0)
		
		if (trkInfTo != trkInfFrom && ! skipMove)
		{
			// move Event to current Track
			trkInfTo->midTrk->AppendEvent(*curEvt);
			trkInfFrom->midTrk->RemoveEvent(curEvt);
		}
	}	// end for (evtIt)
	
	return;
}

// --- General Functions ---
static UINT8 CountDigits(UINT32 value)
{
	UINT8 digits;
	
	digits = 0;
	do
	{
		digits ++;
		value /= 10;
	} while(value);
	
	return digits;
}

static void ModifyTrackNames(std::list<TrackInfo>& trkLst, UINT16 midiTrkID)
{
	if (trkLst.size() <= 1)
		return;
	
	trkinf_iterator trkIt;
	MidiTrack* mainMTrk;
	midevt_iterator evtIt;
	UINT16 trkID;
	UINT16 trkNums;	// Width of TrkCount
	std::string trkName;
	UINT32 newTrkNameLen;
	std::string newTrkName;
	
	trkNums = CountDigits(trkLst.size());
	
	trkName = std::string();
	mainMTrk = trkLst.begin()->midTrk;
	for (evtIt = mainMTrk->GetEventBegin(); evtIt != mainMTrk->GetEventEnd(); ++evtIt)
	{
		if (evtIt->tick > 0)
			break;
		if (evtIt->evtType == 0xFF && evtIt->evtValA == 0x03)
		{
			// Event 'Track Name'
			const char* data = reinterpret_cast<char*>(&evtIt->evtData[0]);
			trkName = std::string(data, data + evtIt->evtData.size());
			break;
		}
	}
	//if (trkName.empty())
	//	return;	// no Track Name found
	
	// New trkName length = trkName length + ' #' + trkNums
	if (trkName.empty())
		newTrkNameLen = 2 + CountDigits(midiTrkID);
	else
		newTrkNameLen = trkName.length();
	newTrkNameLen += 2 + trkNums;
	newTrkName.resize(newTrkNameLen + 0x10);	// just keep some additional buffer
	
	trkID = 0;
	for (trkIt = trkLst.begin(); trkIt != trkLst.end(); ++trkIt)
	{
		trkID ++;	// make numbers with base 1
		if (trkName.empty())
			sprintf(&newTrkName[0], "tk%u #%.*u", midiTrkID, trkNums, trkID);
		else
			sprintf(&newTrkName[0], "%s #%.*u", trkName, trkNums, trkID);
		newTrkNameLen = strlen(newTrkName.c_str());
		
		if (trkIt == trkLst.begin() && ! trkName.empty())
		{
			evtIt->evtData.resize(newTrkNameLen);
			memcpy(&evtIt->evtData[0], newTrkName.c_str(), newTrkNameLen);
		}
		else
		{
			// insert as first event
			trkIt->midTrk->InsertMetaEventD(trkIt->midTrk->GetEventEnd(), 0,
				0x03, newTrkNameLen, newTrkName.c_str());
		}
	}
	
	return;
}

static void AddNoteToList(TrackInfo& trkInf, const MidiEvent& midEvt)
{
	NoteList newNote;
	newNote.note = midEvt.evtValA;
	newNote.chn = midEvt.evtType & 0x0F;
	trkInf.notes.push_back(newNote);
	
	return;
}

static trkinf_iterator RemoveNoteFromList(std::list<TrackInfo>& trkLst, midevt_iterator midEvt)
{
	trkinf_iterator trkIt;
	
	for (trkIt = trkLst.begin(); trkIt != trkLst.end(); ++trkIt)
	{
		std::list<NoteList>::iterator ntIt;
		for (ntIt = trkIt->notes.begin(); ntIt != trkIt->notes.end(); ++ntIt)
		{
			UINT8 evtChn = midEvt->evtType & 0x0F;
			if (ntIt->note == midEvt->evtValA && ntIt->chn == evtChn)
			{
				trkIt->notes.erase(ntIt);
				return trkIt;	// return track of NoteOn event
			}
		}
	}
	
	return trkLst.end();	// note not found
}

UINT8 SplitMidiTracks(UINT8 spltMode)
{
	UINT16 trkCnt;
	UINT16 curTrk;
	std::vector<TrackSplit> trkSplt;
	UINT16 newTrkID;
	
	trkCnt = CMidi.GetTrackCount();
	trkSplt.resize(trkCnt);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		MidiTrack* midiTrk = CMidi.GetTrack(curTrk);
		TrackSplit& curTS = trkSplt[curTrk];
		
		std::cout << "Splitting Track " << curTrk << " ...\n";
		curTS.trkList.clear();
		curTS.trkList.push_back(TrackInfo());
		curTS.trkList.begin()->midTrk = midiTrk;
		
		switch(spltMode)
		{
		case SPLT_BY_NOTE:
			TrkSplit_Note(curTS);
			break;
		case SPLT_BY_INS:
			TrkSplit_Instrument(curTS);
			break;
		}
	}
	
	newTrkID = 0;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		std::list<TrackInfo>& trkLst = trkSplt[curTrk].trkList;
		
		ModifyTrackNames(trkLst, curTrk);
		
		trkinf_iterator trkIt = trkLst.begin();
		// skip first track
		++trkIt;
		newTrkID ++;
		for (; trkIt != trkLst.end(); ++trkIt, newTrkID ++)
		{
			trkIt->midTrk->AppendMetaEvent(0, 0x2F, 0x00, NULL);
			CMidi.Track_Insert(newTrkID, trkIt->midTrk);
		}
	}
	
	trkSplt.clear();
	
	return 0x00;
}
