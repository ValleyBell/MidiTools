// Main File
#include <iostream>
#include <fstream>
#include <string>
#include <math.h>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <algorithm>
#include <cstring>
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
	std::string desc;	// track description
	
	// split by note
	UINT8 notePlaying[0x10];	// stores Note Height of currently playing note
	UINT32 noteTick[0x10];		// for event sorting
	
	UINT8 ins;
	std::list<NoteList> notes;	// note cache for "split by instrument"
};
struct TrackSplit
{
	std::list<TrackInfo> trkList;
};

typedef std::list<TrackInfo>::iterator trkinf_iterator;
typedef std::map<int, trkinf_iterator>::iterator td2trk_iterator;


enum SPLIT_MODES
{
	SPLT_BY_CHN = 0x00,
	SPLT_CHORD = 0x01,
	SPLT_BY_INS = 0x02,
	SPLT_BY_VEL = 0x03,
	SPLT_BY_KEY = 0x04,
};

typedef void (*FuncSplitTrkInit)(TrackInfo& trk, int id);

// Function Prototypes
static void PrepareSplitTrackList(TrackSplit& trkSplt, const std::set<int>& splitIDs,
									FuncSplitTrkInit funcTrackInit, std::map<int, trkinf_iterator>& retId2Trk);
// split by note
static void MoveNoteOn(std::list<TrackInfo>& trkLst, midevt_iterator midEvt);
static void MoveNoteOff(std::list<TrackInfo>& trkLst, midevt_iterator midEvt);
static void TrkSplit_Chord(TrackSplit& trkSplt);
// split by instrument
static void TrkInit_InsSplit(TrackInfo& trk, int id);
static void TrkSplit_Instrument(TrackSplit& trkSplt);
// split by channel
static void TrkInit_ChnSplit(TrackInfo& trk, int id);
static void TrkSplit_Channel(TrackSplit& trkSplt);
// split by volume
static void TrkInit_VelSplit(TrackInfo& trk, int id);
static void TrkSplit_Velocity(TrackSplit& trkSplt);
// split by key
static void TrkInit_KeySplit(TrackInfo& trk, int id);
static void TrkSplit_Key(TrackSplit& trkSplt);
// general
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
		printf("    chn   - split by channel\n");
		printf("    chord - split chords\n");
		printf("    ins   - split by instrument/patch\n");
		printf("    vel   - split by note velocity\n");
		printf("    key   - split by note key\n");
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	UINT8 retVal;
	UINT8 spltMode;
	
	if (! stricmp(argv[1], "Chn"))
		spltMode = SPLT_BY_CHN;
	else if (! stricmp(argv[1], "Chord"))
		spltMode = SPLT_CHORD;
	else if (! stricmp(argv[1], "Ins"))
		spltMode = SPLT_BY_INS;
	else if (! stricmp(argv[1], "Vel"))
		spltMode = SPLT_BY_VEL;
	else if (! stricmp(argv[1], "Key"))
		spltMode = SPLT_BY_KEY;
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

static void PrepareSplitTrackList(TrackSplit& trkSplt, const std::set<int>& splitIDs,
									FuncSplitTrkInit funcTrackInit, std::map<int, trkinf_iterator>& retId2Trk)
{
	size_t trkId;
	trkinf_iterator trkIt;
	
	// 1. create additional tracks
	// Track with ID 0 already exists.
	for (trkId = 1; trkId < splitIDs.size(); trkId ++)
	{
		trkSplt.trkList.push_back(TrackInfo());
		trkIt = trkSplt.trkList.end();
		--trkIt;	// go to last track
		trkIt->midTrk = new MidiTrack;
	}
	
	// 2. splitIDs -> sorted list (or vector) of IDs
	std::vector<int> idList;
	std::set<int>::const_iterator idIt;
	
	idList.reserve(splitIDs.size());
	for (idIt = splitIDs.begin(); idIt != splitIDs.end(); ++idIt)
		idList.push_back(*idIt);
	std::sort(idList.begin(), idList.end());
	
	// 3. create ID -> track lookup table, set track descriptions (funcTrackInit)
	for (trkId = 0, trkIt = trkSplt.trkList.begin(); trkId < idList.size() && trkIt != trkSplt.trkList.end(); ++trkId, ++trkIt)
	{
		retId2Trk[idList[trkId]] = trkIt;
		funcTrackInit(*trkIt, idList[trkId]);
	}
	
	return;
}

// --- Functions for "Split Chords" ---
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
	curTrk->desc = "";
	
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

static void TrkSplit_Chord(TrackSplit& trkSplt)
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
static void TrkInit_InsSplit(TrackInfo& trk, int id)
{
	trk.ins = (UINT8)id;
	{
		char descBuf[0x10];
		sprintf(descBuf, "ins %u", 1 + trk.ins);
		trk.desc = descBuf;
	}
	trk.notes.clear();
	return;
}

static void TrkSplit_Instrument(TrackSplit& trkSplt)
{
	trkinf_iterator trkInfSrc;
	trkinf_iterator trkInfChnDst[0x10];
	MidiTrack* midTrk;
	midevt_iterator evtIt;
	UINT8 chnIns[0x10];
	std::set<int> insSet;
	std::map<int, trkinf_iterator> ins2Trk;
	UINT8 curChn;
	
	// preparse to enumerate all instruments
	midTrk = trkSplt.trkList.begin()->midTrk;
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		chnIns[curChn] = 0xFF;
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); ++evtIt)
	{
		switch(evtIt->evtType & 0xF0)
		{
		case 0x90:
			curChn = evtIt->evtType & 0x0F;
			if (evtIt->evtValB > 0 && chnIns[curChn] == 0xFF)
			{
				chnIns[curChn] = 0x00;
				insSet.emplace(chnIns[curChn]);
			}
			break;
		case 0xC0:
			curChn = evtIt->evtType & 0x0F;
			chnIns[curChn] = evtIt->evtValA;
			insSet.emplace(chnIns[curChn]);
			break;
		}	// end switch(evtIt->evtType & 0xF0)
	}	// end for (evtIt)
	PrepareSplitTrackList(trkSplt, insSet, TrkInit_InsSplit, ins2Trk);
	
	// do actual splitting
	trkInfSrc = trkSplt.trkList.begin();
	midTrk = trkInfSrc->midTrk;
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		trkInfChnDst[curChn] = trkInfSrc;
	
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); )
	{
		trkinf_iterator trkInfDst;
		midevt_iterator curEvt = evtIt;
		++evtIt;	// we may change the track of curEvt
		
		if (curEvt->evtType >= 0xF0)	// don't move SysEx and Meta events
		{
			curChn = 0x00;
			trkInfDst = trkInfSrc;
		}
		else	// move channel-specific instruments based on the channel's current instrument
		{
			curChn = curEvt->evtType & 0x0F;
			trkInfDst = trkInfChnDst[curChn];
		}
		
		switch(curEvt->evtType & 0xF0)
		{
		case 0x80:
		case 0x90:
			if ((curEvt->evtType & 0xF0) == 0x90 && curEvt->evtValB)
			{
				AddNoteToList(*trkInfDst, *curEvt);
			}
			else
			{
				trkinf_iterator noteOnTrk = RemoveNoteFromList(trkSplt.trkList, curEvt);
				if (noteOnTrk != trkSplt.trkList.end())
					trkInfDst = noteOnTrk;	// move NoteOff event to track of NoteOn event
			}
			break;
		case 0xC0:
			{
				// find a track that uses the new instrument
				td2trk_iterator mapIt = ins2Trk.find(curEvt->evtValA);
				trkInfDst = (mapIt != ins2Trk.end()) ? mapIt->second : trkInfSrc;
			}
			trkInfChnDst[curChn] = trkInfDst;
			break;
		}	// end switch(curEvt->evtType & 0xF0)
		
		if (trkInfDst != trkInfSrc)
		{
			// move Event to current Track
			trkInfDst->midTrk->AppendEvent(*curEvt);
			trkInfSrc->midTrk->RemoveEvent(curEvt);
		}
	}	// end for (evtIt)
	
	return;
}

// --- Functions for "Split by Channel" ---
static void TrkInit_ChnSplit(TrackInfo& trk, int id)
{
	UINT8 chn = (UINT8)id;
	{
		char descBuf[0x10];
		sprintf(descBuf, "ch %u", 1 + chn);
		trk.desc = descBuf;
	}
	return;
}

static void TrkSplit_Channel(TrackSplit& trkSplt)
{
	trkinf_iterator trkInfSrc;
	MidiTrack* midTrk;
	midevt_iterator evtIt;
	std::set<int> chnSet;
	std::map<int, trkinf_iterator> chn2Trk;
	UINT8 curChn;
	
	// preparse to enumerate all instruments
	midTrk = trkSplt.trkList.begin()->midTrk;
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); ++evtIt)
	{
		if (evtIt->evtType < 0xF0)
		{
			curChn = evtIt->evtType & 0x0F;
			chnSet.emplace(curChn);
		}
		else
		{
			if (evtIt->evtType == 0xFF && evtIt->evtValA == 0x20)
			{
				if (evtIt->evtData.size() >= 1)
				{
					curChn = evtIt->evtData[0x00] & 0x0F;
					chnSet.emplace(curChn);
				}
			}
		}
	}	// end for (evtIt)
	PrepareSplitTrackList(trkSplt, chnSet, TrkInit_ChnSplit, chn2Trk);
	
	// do actual splitting
	trkInfSrc = trkSplt.trkList.begin();
	midTrk = trkInfSrc->midTrk;
	
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); )
	{
		trkinf_iterator trkInfChnDst;
		midevt_iterator curEvt = evtIt;
		++evtIt;	// we may change the track of curEvt
		
		if (curEvt->evtType < 0xF0)
		{
			curChn = curEvt->evtType & 0x0F;
		}
		else
		{
			curChn = 0xFF;
			if (curEvt->evtType == 0xFF && curEvt->evtValA == 0x20)
			{
				if (curEvt->evtData.size() >= 1)
					curChn = curEvt->evtData[0x00] & 0x0F;
			}
		}
		td2trk_iterator mapIt = chn2Trk.find(curChn);
		trkInfChnDst = (mapIt != chn2Trk.end()) ? mapIt->second : trkInfSrc;
		if (trkInfChnDst != trkInfSrc)
		{
			// move Event to current Track
			trkInfChnDst->midTrk->AppendEvent(*curEvt);
			trkInfSrc->midTrk->RemoveEvent(curEvt);
		}
	}	// end for (evtIt)
	
	return;
}

// --- Functions for "Split by Volume" ---
static void TrkInit_VelSplit(TrackInfo& trk, int id)
{
	UINT8 vel = (UINT8)(-id);
	{
		char descBuf[0x10];
		sprintf(descBuf, "vol %u", vel);
		trk.desc = descBuf;
	}
	trk.notes.clear();
	return;
}

static void TrkSplit_Velocity(TrackSplit& trkSplt)
{
	trkinf_iterator trkInfSrc;
	trkinf_iterator trkInfChnDst[0x10];
	MidiTrack* midTrk;
	midevt_iterator evtIt;
	std::set<int> volSet;
	std::map<int, trkinf_iterator> vol2Trk;
	UINT8 curChn;
	
	// preparse to enumerate all volume values
	midTrk = trkSplt.trkList.begin()->midTrk;
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); ++evtIt)
	{
		switch(evtIt->evtType & 0xF0)
		{
		case 0x90:
			if (evtIt->evtValB > 0)
				volSet.emplace(evtIt->evtValB * -1);	// *-1 for sorting from high to low volume
			break;
		}	// end switch(evtIt->evtType & 0xF0)
	}	// end for (evtIt)
	PrepareSplitTrackList(trkSplt, volSet, TrkInit_VelSplit, vol2Trk);
	
	// do actual splitting
	trkInfSrc = trkSplt.trkList.begin();
	midTrk = trkInfSrc->midTrk;
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		trkInfChnDst[curChn] = trkInfSrc;
	
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); )
	{
		trkinf_iterator trkInfDst;
		midevt_iterator curEvt = evtIt;
		++evtIt;	// we may change the track of curEvt
		
		// TODO: Allow moving Control Change channel events as well.
		trkInfDst = trkInfSrc;
		switch(curEvt->evtType & 0xF0)
		{
		case 0x80:
		case 0x90:
			if ((curEvt->evtType & 0xF0) == 0x90 && curEvt->evtValB)
			{
				// Note On
				td2trk_iterator mapIt = vol2Trk.find(curEvt->evtValB * -1);
				trkInfDst = (mapIt != vol2Trk.end()) ? mapIt->second : trkInfSrc;
				AddNoteToList(*trkInfDst, *curEvt);
				
				curChn = curEvt->evtType & 0x0F;
				trkInfChnDst[curChn] = trkInfDst;
			}
			else
			{
				// Note Off
				trkinf_iterator noteOnTrk = RemoveNoteFromList(trkSplt.trkList, curEvt);
				if (noteOnTrk != trkSplt.trkList.end())
					trkInfDst = noteOnTrk;	// move NoteOff event to track of NoteOn event
			}
			break;
		case 0xB0:	// Control Change
			switch(curEvt->evtValA)
			{
			case 0x07:	// Main Volume
			case 0x0B:	// Expression
				break;
			}
			break;
		case 0xE0:	// Pitch Bend
			// move pitch bends along with the note they are applied to
			curChn = curEvt->evtType & 0x0F;
			trkInfDst = trkInfChnDst[curChn];
			break;
		}
		
		if (trkInfDst != trkInfSrc)
		{
			// move Event to current Track
			trkInfDst->midTrk->AppendEvent(*curEvt);
			trkInfSrc->midTrk->RemoveEvent(curEvt);
		}
	}	// end for (evtIt)
	
	return;
}

// --- Functions for "Split by Key" ---
static void TrkInit_KeySplit(TrackInfo& trk, int id)
{
	UINT8 key = (UINT8)id;
	{
		char descBuf[0x10];
		sprintf(descBuf, "note %u", key);
		trk.desc = descBuf;
	}
	trk.notes.clear();
	return;
}

static void TrkSplit_Key(TrackSplit& trkSplt)
{
	trkinf_iterator trkInfSrc;
	trkinf_iterator trkInfChnDst[0x10];
	MidiTrack* midTrk;
	midevt_iterator evtIt;
	std::set<int> keySet;
	std::map<int, trkinf_iterator> key2Trk;
	UINT8 curChn;
	
	// preparse to enumerate all key values
	midTrk = trkSplt.trkList.begin()->midTrk;
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); ++evtIt)
	{
		switch(evtIt->evtType & 0xF0)
		{
		case 0x90:
			if (evtIt->evtValB > 0)	// only conider Note On events
				keySet.emplace(evtIt->evtValA);
			break;
		}	// end switch(evtIt->evtType & 0xF0)
	}	// end for (evtIt)
	PrepareSplitTrackList(trkSplt, keySet, TrkInit_KeySplit, key2Trk);
	
	// do actual splitting
	trkInfSrc = trkSplt.trkList.begin();
	midTrk = trkInfSrc->midTrk;
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		trkInfChnDst[curChn] = trkInfSrc;
	
	for (evtIt = midTrk->GetEventBegin(); evtIt != midTrk->GetEventEnd(); )
	{
		trkinf_iterator trkInfDst;
		midevt_iterator curEvt = evtIt;
		++evtIt;	// we may change the track of curEvt
		
		// TODO: Allow moving Control Change channel events as well.
		trkInfDst = trkInfSrc;
		switch(curEvt->evtType & 0xF0)
		{
		case 0x80:
		case 0x90:
			if ((curEvt->evtType & 0xF0) == 0x90 && curEvt->evtValB)
			{
				// Note On
				td2trk_iterator mapIt = key2Trk.find(curEvt->evtValA);
				trkInfDst = (mapIt != key2Trk.end()) ? mapIt->second : trkInfSrc;
				AddNoteToList(*trkInfDst, *curEvt);
				
				curChn = curEvt->evtType & 0x0F;
				trkInfChnDst[curChn] = trkInfDst;
			}
			else
			{
				// Note Off
				trkinf_iterator noteOnTrk = RemoveNoteFromList(trkSplt.trkList, curEvt);
				if (noteOnTrk != trkSplt.trkList.end())
					trkInfDst = noteOnTrk;	// move NoteOff event to track of NoteOn event
			}
			break;
		case 0xE0:	// Pitch Bend
			// move pitch bends along with the note they are applied to
			curChn = curEvt->evtType & 0x0F;
			trkInfDst = trkInfChnDst[curChn];
			break;
		}
		
		if (trkInfDst != trkInfSrc)
		{
			// move Event to current Track
			trkInfDst->midTrk->AppendEvent(*curEvt);
			trkInfSrc->midTrk->RemoveEvent(curEvt);
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

static std::string GenerateTkName(const std::string& trkName, UINT16 trkID)
{
	if (! trkName.empty())
		return trkName;
	
	char nameBuf[0x10];
	sprintf(nameBuf, "tk%u", trkID);
	return std::string(nameBuf);
}

static void ModifyTrackNames(std::list<TrackInfo>& trkLst, UINT16 midiTrkID)
{
	if (trkLst.size() <= 1)
		return;
	
	trkinf_iterator trkIt;
	MidiTrack* mainMTrk;
	midevt_iterator evtIt;
	midevt_iterator tNameEvtIt;
	UINT16 trkID;
	UINT16 trkNums;	// Width of TrkCount
	std::string trkName;
	
	trkNums = CountDigits(trkLst.size());
	
	trkName = std::string();
	mainMTrk = trkLst.begin()->midTrk;
	tNameEvtIt = mainMTrk->GetEventEnd();
	for (evtIt = mainMTrk->GetEventBegin(); evtIt != mainMTrk->GetEventEnd(); ++evtIt)
	{
		if (evtIt->tick > 0)
			break;
		if (evtIt->evtType == 0xFF && evtIt->evtValA == 0x03)
		{
			// Event 'Track Name'
			const char* data = reinterpret_cast<char*>(&evtIt->evtData[0]);
			trkName = std::string(data, data + evtIt->evtData.size());
			tNameEvtIt = evtIt;
			break;
		}
	}
	if (trkName.empty())
		trkName = GenerateTkName(trkName, midiTrkID);
	
	trkID = 0;
	for (trkIt = trkLst.begin(); trkIt != trkLst.end(); ++trkIt)
	{
		trkID ++;	// make numbers with base 1
		if (trkIt->desc.empty())
		{
			char trkSubName[0x10];
			sprintf(trkSubName, "#%.*u", trkNums, trkID);
			trkIt->desc = std::string(trkSubName);
		}
		std::string newTrkName = trkName + " " + trkIt->desc;
		
		if (trkIt == trkLst.begin() && tNameEvtIt != trkIt->midTrk->GetEventEnd())
		{
			evtIt->evtData.resize(newTrkName.size());
			memcpy(&evtIt->evtData[0], newTrkName.c_str(), newTrkName.size());
		}
		else
		{
			// insert as first event
			trkIt->midTrk->InsertMetaEventD(trkIt->midTrk->GetEventEnd(), 0,
				0x03, newTrkName.size(), newTrkName.c_str());
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
		case SPLT_CHORD:
			TrkSplit_Chord(curTS);
			break;
		case SPLT_BY_INS:
			TrkSplit_Instrument(curTS);
			break;
		case SPLT_BY_CHN:
			TrkSplit_Channel(curTS);
			break;
		case SPLT_BY_VEL:
			TrkSplit_Velocity(curTS);
			break;
		case SPLT_BY_KEY:
			TrkSplit_Key(curTS);
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
