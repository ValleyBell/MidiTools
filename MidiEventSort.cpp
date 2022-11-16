#define _USE_MATH_DEFINES
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <ctype.h>	// for tolower()
#include <math.h>

#include <stdtype.h>
#include "MidiLib.hpp"

struct EvtSortInfo
{
	UINT32 sortID;
	UINT32 order;
	midevt_iterator evt;
};


// Function Prototypes
void MidiEventSort(void);
static void SortEvents(MidiTrack* midiTrk, midevt_iterator startIt, midevt_iterator endIt);
static void ReorderEvents(MidiTrack* midiTrk, midevt_iterator startIt, midevt_iterator endIt, std::vector<EvtSortInfo> sortList);


#define EVTSORT_NOTES		0x01
#define EVTSORT_CTRLS		0x02

static UINT8 EVT_SORT_MASK;
MidiFile CMidi;

int main(int argc, char* argv[])
{
	int argbase;
	
	std::cout << "MIDI Event Sorter\n";
	std::cout << "-----------------\n";
	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " [options] input.mid output.mid\n";
		std::cout << "Options:\n";
		std::cout << "    -e mask - bitmask of events to be sorted (default: 0x00)\n";
		std::cout << "              0x01 - sort controllers by ID\n";
		std::cout << "              0x02 - sort notes by pitch\n";
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	argbase = 1;
	EVT_SORT_MASK = 0x00;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		char optChr = tolower(argv[argbase][1]);
		
		if (optChr == 'e')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			EVT_SORT_MASK = (UINT8)strtol(argv[argbase], NULL, 0);
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
	
	MidiEventSort();
	
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

void MidiEventSort(void)
{
	UINT16 trkCnt;
	UINT16 curTrk;
	
	trkCnt = CMidi.GetTrackCount();
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		MidiTrack* midiTrk = CMidi.GetTrack(curTrk);
		midevt_iterator evtIt;
		midevt_iterator tickStIt;
		
		tickStIt = midiTrk->GetEventBegin();
		for (evtIt = midiTrk->GetEventBegin(); evtIt != midiTrk->GetEventEnd(); ++evtIt)
		{
			UINT8 evtChn = evtIt->evtType & 0x0F;
			switch(evtIt->evtType & 0xF0)
			{
			case 0x80:
			case 0x90:
				break;
			case 0xB0:
				break;
			}
			if (evtIt->tick > tickStIt->tick)
			{
				SortEvents(midiTrk, tickStIt, evtIt);
				tickStIt = evtIt;
			}
		}	// end while(evtIt)
	}
	
	return;
}

static bool evtsort_compare(const EvtSortInfo& first, const EvtSortInfo& second)
{
	return (first.sortID < second.sortID);
}

static UINT32 GetEvtSortID(const MidiEvent& evt)
{
	UINT8 evtChn = evt.evtType & 0x0F;
	UINT32 tmpID;
	
	switch(evt.evtType & 0xF0)
	{
	case 0x80:	// Note Off
	case 0x90:	// Note On
		tmpID = 0;
		if (EVT_SORT_MASK & EVTSORT_NOTES)
			tmpID |= (evt.evtValA << 4);
		if ((evt.evtType & 0x10) && evt.evtValB > 0)	// Note On?
			return 0xF000 | tmpID | (evtChn << 0);	// place last
		else	// Note Off
			return 0x0000 | tmpID | (evtChn << 0);	// place first
	case 0xA0:
		return 0x3000 | (evtChn << 8);
	case 0xB0:
		if (evt.evtValA >= 0x78)	// mode change
			return (UINT32)-1;	// don't relocate
		else if (evt.evtValA >= 0x60 && evt.evtValA <= 0x65)	// Data Increment/Decrement/NRPN/RPN
			return (UINT32)-1;	// don't relocate
		
		if (evt.evtValA < 0x40)
		{
			UINT8 ctrlID = evt.evtValA & 0x1F;
			UINT8 mlsb = (evt.evtValA & 0x20) >> 5;
			if (ctrlID == 0x00)	// Bank Select
				return 0x1000 | (evtChn << 8) | (mlsb << 0);	// place before Instrument Change
			else if (ctrlID == 0x06)	// Data MSB/LSB
				return (UINT32)-1;	// don't relocate
			
			tmpID = 0;
			if (EVT_SORT_MASK & EVTSORT_CTRLS)
				tmpID |= (ctrlID << 1) | (mlsb << 0);
			return 0x2000 | (evtChn << 8) | tmpID;
		}
		return 0x2000 | (evtChn << 8) | (evt.evtValA << 0);
	case 0xC0:
		return 0x1002 | (evtChn << 8);
	case 0xD0:
		return 0x3001 | (evtChn << 8);
	case 0xE0:
		return 0x3002 | (evtChn << 8);
	case 0xF0:
		return (UINT32)-1;	// don't relocate
	default:
		return (UINT32)-1;	// don't relocate
	}
}

static void SortEvents(MidiTrack* midiTrk, midevt_iterator startIt, midevt_iterator endIt)
{
	std::vector<EvtSortInfo> sortList;
	midevt_iterator evtIt;
	
	for (evtIt = startIt; evtIt != endIt; ++evtIt)
	{
		EvtSortInfo esi;
		esi.sortID = GetEvtSortID(*evtIt);
		esi.evt = evtIt;
		
		if (esi.sortID == (UINT32)-1)
		{
			if (sortList.size() >= 1)
				ReorderEvents(midiTrk, sortList[0].evt, evtIt, sortList);
			sortList.clear();
		}
		else
		{
			sortList.push_back(esi);
		}
	}
	if (sortList.size() > 1)
		ReorderEvents(midiTrk, sortList[0].evt, evtIt, sortList);
	
	return;
}

static void ReorderEvents(MidiTrack* midiTrk, midevt_iterator startIt, midevt_iterator endIt, std::vector<EvtSortInfo> sortList)
{
	UINT32 curEvt;
	for (curEvt = 0; curEvt < sortList.size(); curEvt ++)
		sortList[curEvt].order = curEvt;
	std::stable_sort(sortList.begin(), sortList.end(), evtsort_compare);
	
	midevt_iterator evtIt = startIt;
	for (curEvt = 0; curEvt < sortList.size(); curEvt ++)
	{
		const EvtSortInfo& esi = sortList[curEvt];
		// don't try to move events that are at the correct place already, prevents infinite loops
		if (evtIt == esi.evt)
		{
			++evtIt;
			continue;
		}
		
		MidiEvent evtData = *esi.evt;
		midiTrk->RemoveEvent(esi.evt);
		if (evtIt == midiTrk->GetEventBegin())
		{
			midiTrk->InsertEventT(evtData);
		}
		else
		{
			midevt_iterator prevEvt = evtIt;
			--prevEvt;
			midiTrk->InsertEventD(prevEvt, evtData);
		}
	}
	
	return;
}
