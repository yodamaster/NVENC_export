/*
 * Copyright 1993-2012 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

#include "FrameQueue.h"
#include <assert.h>

#define ENABLE_DEBUG_OUT 0

#if ENABLE_DEBUG_OUT
#include <iostream>
#define dbgprintf(x) printf x
#else
#define dbgprintf(x)
#endif

FrameQueue::FrameQueue(): hEvent_(0)
    , nReadPosition_(0)
    , nFramesInQueue_(0)
    , bEndOfDecode_(0)
{
#ifdef WIN32
    hEvent_ = CreateEvent(NULL, false, false, NULL);
    InitializeCriticalSection(&oCriticalSection_);
#endif
    Clear(); // clear internal-state arrays
}

FrameQueue::~FrameQueue()
{
#ifdef WIN32
    DeleteCriticalSection(&oCriticalSection_);
    CloseHandle(hEvent_);
#endif
}

void
FrameQueue::waitForQueueUpdate()
{
#ifdef WIN32
    WaitForSingleObject(hEvent_, 10);
#endif
}

void
FrameQueue::enter_CS(CRITICAL_SECTION *pCS)
{
#ifdef WIN32
    EnterCriticalSection(pCS);
#endif
}


void
FrameQueue::leave_CS(CRITICAL_SECTION *pCS)
{
#ifdef WIN32
    LeaveCriticalSection(pCS);
#endif
}

void
FrameQueue::set_event(HANDLE event)
{
#ifdef WIN32
    SetEvent(event);
#endif
}

void
FrameQueue::reset_event(HANDLE event)
{
#ifdef WIN32
    ResetEvent(event);
#endif
}

void
FrameQueue::enqueue(const CUVIDPARSERDISPINFO *pPicParams)
{
    // Mark the frame as 'in-use' so we don't re-use it for decoding until it is no longer needed
    // for display
    aIsFrameInUse_[pPicParams->picture_index] = true;

    // Wait until we have a free entry in the display queue (should never block if we have enough entries)
    do
    {
        bool bPlacedFrame = false;
        enter_CS(&oCriticalSection_);

        if (nFramesInQueue_ < (int)FrameQueue::cnMaximumSize)
        {
            int iWritePosition = (nReadPosition_ + nFramesInQueue_) % cnMaximumSize;
            aDisplayQueue_[iWritePosition] = *pPicParams;
            nFramesInQueue_++;
            bPlacedFrame = true;
        }

        leave_CS(&oCriticalSection_);

        if (bPlacedFrame) // Done
            break;

        sleep(1);   // Wait a bit
    }
    while (!bEndOfDecode_);

    signalStatusChange();  // Signal for the display thread
}

// if no valid picture can be return the pic-info's picture_index will
// be -1.
bool
FrameQueue::dequeue(CUVIDPARSERDISPINFO *pDisplayInfo, CUVIDPICPARAMS *pPicParams)
{
    pDisplayInfo->picture_index = -1;
    bool bHaveNewFrame = false;

    enter_CS(&oCriticalSection_);

    if (nFramesInQueue_ > 0)
    {
        int iEntry = nReadPosition_;
        *pDisplayInfo = aDisplayQueue_[iEntry];
        // fetch the metadata for the decoded picture. (also Removes it from internal FIFO)
        popCuVidPicParams( pPicParams);
        nReadPosition_ = (iEntry+1) % cnMaximumSize;
        nFramesInQueue_--;
        bHaveNewFrame = true;
    }

    leave_CS(&oCriticalSection_);

    return bHaveNewFrame;
}

void
FrameQueue::releaseFrame(const CUVIDPARSERDISPINFO *pPicParams)
{
    aIsFrameInUse_[pPicParams->picture_index] = false;
}

void
FrameQueue::releaseFrame(const int picture_index )
{
    aIsFrameInUse_[picture_index] = false;
}

bool
FrameQueue::isInUse(int nPictureIndex)
const
{
    assert(nPictureIndex >= 0);
    assert(nPictureIndex < (int)cnMaximumSize);

    return (0 != aIsFrameInUse_[nPictureIndex]);
}

bool
FrameQueue::isEndOfDecode()
const
{
    return (0 != bEndOfDecode_);
}

void
FrameQueue::endDecode()
{
    bEndOfDecode_ = true;
    signalStatusChange();  // Signal for the display thread
}

// Spins until frame becomes available or decoding
// gets canceled.
// If the requested frame is available the method returns true.
// If decoding was interupted before the requested frame becomes
// available, the method returns false.
bool
FrameQueue::waitUntilFrameAvailable(int nPictureIndex)
{
    while (isInUse(nPictureIndex))
    {
        sleep(1);   // Decoder is getting too far ahead from display

        if (isEndOfDecode())
            return false;
    }

    return true;
}

void
FrameQueue::signalStatusChange()
{
    set_event(hEvent_);
}

bool
FrameQueue::pushCuVidPicParams( CUVIDPICPARAMS *pPicParams)
{
    enter_CS(&oCriticalSection_);

    qDecodePicParams_.push_back( *pPicParams );

    leave_CS(&oCriticalSection_);

    return true;
}

bool
FrameQueue::popCuVidPicParams( CUVIDPICPARAMS *pPicParams)
{
    bool found = false;

    // the following code should be guarded by CRITICAL_REGION
    // Since this code is only called by another function that is
    // already in critical region, here we safely leave that out.

    if ( qDecodePicParams_.size() ) {
        *pPicParams = qDecodePicParams_.front();
        qDecodePicParams_.pop_front();
        found = true;
    }
    else {
        // empty queue (this should never happen)
        memset((void *)pPicParams, 0, sizeof(CUVIDPICPARAMS) );
        pPicParams->CurrPicIdx = -1; // mark this whole structure as BAD
    }

    return found;
}

void
FrameQueue::Clear( )
{
    // This function should only be called when the decode-pipeline is idle (i.e.
    // no other activity into this class.)

    enter_CS(&oCriticalSection_);

    qDecodePicParams_.clear();

    memset(aDisplayQueue_, 0, cnMaximumSize * sizeof(CUVIDPARSERDISPINFO));
    memset((void *)aIsFrameInUse_, 0, cnMaximumSize * sizeof(int));

    leave_CS(&oCriticalSection_);
}

