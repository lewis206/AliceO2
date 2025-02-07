// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// @file   TrackletParser.h
/// @brief  TRD raw data parser for Tracklet data format

#include "DataFormatsTRD/RawData.h"
#include "DataFormatsTRD/Tracklet64.h"

#include "TRDReconstruction/TrackletsParser.h"
#include "fairlogger/Logger.h"

//TODO come back and figure which of below headers I actually need.
#include <cstring>
#include <string>
#include <vector>
#include <array>

namespace o2::trd
{

inline void TrackletsParser::swapByteOrder(unsigned int& ui)
{
  ui = (ui >> 24) |
       ((ui << 8) & 0x00FF0000) |
       ((ui >> 8) & 0x0000FF00) |
       (ui << 24);
}

int TrackletsParser::Parse()
{
  //we are handed the buffer payload of an rdh and need to parse its contents.
  //producing a vector of digits.
  if (mVerbose) {
    LOG(info) << "Tracklet Parser parse of data sitting at :" << std::hex << (void*)mData << " starting at pos " << mStartParse;
    if (mByteOrderFix) {

      LOG(info) << " we will be byte swapping";
    } else {

      LOG(info) << " we will *not* be byte swapping";
    }
  }
  if (mDataVerbose) {
    LOG(info) << "trackletdata to parse begin";
    std::vector<uint32_t> datacopy(mStartParse, mEndParse);
    if (mByteOrderFix) {
      for (auto a : datacopy) {
        swapByteOrder(a);
      }
    }

    LOG(info) << "trackletdata to parse with size of " << datacopy.size();
    int loopsize = 0;
    if (datacopy.size() > 1024) {
      loopsize = 64;
    }
    for (int i = 0; i < loopsize; i += 8) {
      LOG(info) << std::hex << "0x" << datacopy[i] << " " << std::hex << "0x" << datacopy[i + 1] << " " << std::hex << "0x" << datacopy[i + 2] << " " << std::hex << "0x" << datacopy[i + 3] << " " << std::hex << "0x" << datacopy[i + 4] << " " << std::hex << "0x" << datacopy[i + 5] << " " << std::hex << "0x" << datacopy[i + 6] << " " << std::hex << "0x" << datacopy[i + 7];
    }
    LOG(info) << "trackletdata to parse end";
    if (datacopy.size() > 4096) {
      LOG(fatal) << "something very wrong with tracklet parsing >4096";
    }
  }

  //mData holds a buffer containing tracklets parse placing tracklets in the output vector.
  //mData holds 2048 digits.
  mCurrentLink = 0;
  mWordsRead = 0;
  mTrackletsFound = 0;
  if (mTrackletHCHeaderState == 0) {
    // tracklet hc header is never present
    mState = StateTrackletMCMHeader;
  } else {
    if (mTrackletHCHeaderState == 1) {
      auto nextword = std::next(mStartParse);
      if (*nextword != constants::TRACKLETENDMARKER) {
        //we have tracklet data so no TracletHCHeader
        mState = StateTrackletHCHeader;
      } else {
        //we have no tracklet data so no TracletHCHeader
        mState = StateTrackletMCMHeader;
      }
    } else {
      if (mTrackletHCHeaderState != 2) {
        LOG(warn) << "unknwon TrackletHCHeaderState of " << mIgnoreTrackletHCHeader;
      }
      // tracklet hc header is always present
      mState = StateTrackletHCHeader; // we start with a trackletMCMHeader
    }
  }

  int currentLinkStart = 0;
  int mcmtrackletcount = 0;
  int trackletloopcount = 0;
  int headertrackletcount = 0;
  if (mDataVerbose) {
    LOG(info) << "distance to parse over is " << std::distance(mStartParse, mEndParse);
  }
  for (auto word = mStartParse; word != mEndParse; word++) { // loop over the entire data buffer (a complete link of tracklets and digits)
    if (mState == StateFinished) {
      return mWordsRead;
    }
    //loop over all the words ...
    //check for tracklet end marker 0x1000 0x1000
    int index = std::distance(mStartParse, word);
    int indexend = std::distance(word, mEndParse);
    std::array<uint32_t, o2::trd::constants::HBFBUFFERMAX>::iterator nextword = word;
    std::advance(nextword, 1);
    uint32_t nextwordcopy = *nextword;

    if (mByteOrderFix) {
      swapByteOrder(*word);
      swapByteOrder(nextwordcopy);
    }
    if (mDataVerbose) {
      if (mByteOrderFix) {
        LOG(info) << "After byteswapping " << index << " word is : " << std::hex << word << " next word is : " << nextwordcopy << " and raw nextword is :" << std::hex << (*mData)[index + 1];
      } else {
        LOG(info) << "After byteswapping " << index << " word is : " << std::hex << word << " next word is : " << nextwordcopy << " and raw nextword is :" << std::hex << (*mData)[index + 1];
      }
    }

    if (*word == 0x10001000 && nextwordcopy == 0x10001000) {
      if (!StateTrackletEndMarker && !StateTrackletHCHeader) {
        LOG(warn) << "State should be trackletend marker current ?= end marker  ?? " << mState << " ?=" << StateTrackletEndMarker;
      }
      mWordsRead += 2;
      //we should now have a tracklet half chamber header.
      mState = StateTrackletHCHeader;
      std::array<uint32_t, o2::trd::constants::HBFBUFFERMAX>::iterator hchword = word;
      std::advance(hchword, 2);
      uint32_t halfchamberheaderint = *hchword;
      mState = StateTrackletEndMarker;
      return mWordsRead;
    }
    if (*word == o2::trd::constants::CRUPADDING32) {
      //padding word first as it clashes with the hcheader.
      mState = StatePadding;
      mWordsRead++;
      LOG(warn) << "CRU Padding word while parsing tracklets. This should *never* happen, this should happen after the tracklet end markers when we are outside the tracklet parsing";
    } else {
      //now for Tracklet hc header
      if ((((*word) & (0x1 << 11)) != 0) && !mIgnoreTrackletHCHeader && mState == StateTrackletHCHeader) { //TrackletHCHeader has bit 11 set to 1 always. Check for state because raw data can have bit 11 set!
        if (mState != StateTrackletHCHeader) {
          LOG(warn) << "Something wrong with TrackletHCHeader bit 11 is set but state is not " << StateTrackletMCMHeader << " its :" << mState;
        }
        //read the header
        //we actually have a header word.
        mTrackletHCHeader = (TrackletHCHeader*)&word;
        if (mHeaderVerbose) {
          LOG(info) << "state trackletHCheader and word : 0x" << std::hex << *word << " sanity check : " << trackletHCHeaderSanityCheck(*mTrackletHCHeader);
        }
        //sanity check of trackletheader ??
        if (!trackletHCHeaderSanityCheck(*mTrackletHCHeader)) {
          LOG(warn) << "Sanity check Failure HCHeader : " << std::hex << *word;
        }
        mWordsRead++;
        mState = StateTrackletMCMHeader;                                // now we should read a MCMHeader next time through loop
      } else {                                                          //not TrackletMCMHeader
        if ((*word) & 0x80000001 && mState == StateTrackletMCMHeader) { //TrackletMCMHeader has the bits on either end always 1
          //mcmheader
          mTrackletMCMHeader = (TrackletMCMHeader*)&(*word);
          if (mHeaderVerbose) {
            LOG(info) << "state mcmheader and word : 0x" << std::hex << *word;
            TrackletMCMHeader a;
            a.word = *word;
            printTrackletMCMHeader(a);
          }
          headertrackletcount = getNumberofTracklets(*mTrackletMCMHeader);
          mState = StateTrackletMCMData; // afrter reading a header we should then have data for next round through the loop
          mcmtrackletcount = 0;
          mWordsRead++;
        } else {
          mState = StateTrackletMCMData;
          //tracklet data;
          mTrackletMCMData = (TrackletMCMData*)&(*word);
          if (mDataVerbose) {
            LOG(info) << std::hex << *word << "  read a raw tracklet from the raw stream mcmheader ";
            printTrackletMCMData(*mTrackletMCMData);
          }
          mWordsRead++;
          // take the header and this data word and build the underlying 64bit tracklet.
          int q0, q1, q2;
          int qa, qb;
          switch (mcmtrackletcount) {
            case 0:
              qa = mTrackletMCMHeader->pid0;
              break;
            case 1:
              qa = mTrackletMCMHeader->pid1;
              break;
            case 2:
              qa = mTrackletMCMHeader->pid2;
              break;
            default:
              LOG(warn) << "mcmtrackletcount is not in [0:2] count=" << mcmtrackletcount << " headertrackletcount=" << headertrackletcount << " something very wrong parsing the TrackletMCMData fields with data of : 0x" << std::hex << mTrackletMCMData->word;
              break;
          }
          q0 = getQFromRaw(mTrackletMCMHeader, mTrackletMCMData, 0, mcmtrackletcount);
          q1 = getQFromRaw(mTrackletMCMHeader, mTrackletMCMData, 1, mcmtrackletcount);
          q2 = getQFromRaw(mTrackletMCMHeader, mTrackletMCMData, 2, mcmtrackletcount);
          int padrow = mTrackletMCMHeader->padrow;
          int col = mTrackletMCMHeader->col;
          int pos = mTrackletMCMData->pos;
          int slope = mTrackletMCMData->slope;
          int hcid = mDetector * 2 + mRobSide;
          if (mDataVerbose) {
            LOG(info) << "Tracklet HCID : " << hcid << " mDetector:" << mDetector << " robside:" << mRobSide << " " << mTrackletMCMHeader->padrow << ":" << mTrackletMCMHeader->col << " ---- " << mTrackletHCHeader->supermodule << ":" << mTrackletHCHeader->stack << ":" << mTrackletHCHeader->layer << ":" << mTrackletHCHeader->side << " rawhcheader : 0x" << std::hex << std::hex << mTrackletHCHeader->word;
          }
          //TODO cross reference hcid to somewhere for a check. mDetector is assigned at the time of parser init.
          //
          mTracklets.emplace_back(4, hcid, padrow, col, pos, slope, q0, q1, q2); // our format is always 4
          if (mDataVerbose) {
            LOG(info) << "Tracklet added:" << 4 << "-" << hcid << "-" << padrow << "-" << col << "-" << pos << "-" << slope << "-" << q0 << ":" << q1 << ":" << q2;
          }
          mTrackletsFound++;
          mcmtrackletcount++;
          if (mcmtrackletcount == headertrackletcount) { // headertrackletcount and mcmtrackletcount are not zero based counting
            // at the end of the tracklet output of this mcm
            // next to come can either be an mcmheaderword or a trackletendmarker.
            // check next word if its a trackletendmarker
            auto nextdataword = std::next(word, 1);
            // the check is unambigous between trackletendmarker and mcmheader
            if ((*nextdataword) == constants::TRACKLETENDMARKER) {
              mState = StateTrackletEndMarker;
            } else {
              mState = StateTrackletMCMHeader;
            }
          }
          if (mcmtrackletcount > 3) {
            LOG(warn) << "We have more than 3 Tracklets in parsing the TrackletMCMData attached to a single TrackletMCMHeader";
          }
        }
      }
    } // else
    trackletloopcount++;
  } //end of for loop
  //sanity check
  LOG(warn) << " end of Trackelt parsing but we are exiting with out a tracklet end marker with " << mWordsRead << " 32bit words read";
  return mWordsRead;
}

} // namespace o2::trd
