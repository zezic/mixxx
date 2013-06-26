/**
 * \file soundsource.cpp
 * \author Albert Santoni <alberts at mixxx dot org>
 * \date Dec 12, 2010
 */

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QtDebug>
#include <QUrl>
#include <taglib/mpegfile.h>
#include <taglib/mp4file.h>

#include "soundsourcecoreaudio.h"

SoundSourceCoreAudio::SoundSourceCoreAudio(QString filename)
    : Mixxx::SoundSource(filename)
    , m_file(filename)
    , m_samples(0)
    , m_headerFrames(0)
{
}

SoundSourceCoreAudio::~SoundSourceCoreAudio() {
	ExtAudioFileDispose(m_audioFile);

}

// soundsource overrides
int SoundSourceCoreAudio::open() {
    //m_file.open(QIODevice::ReadOnly);

    //Open the audio file.
    OSStatus err;

	//QUrl blah(m_qFilename);
    QString qurlStr = m_qFilename;//blah.toString();
    qDebug() << qurlStr;

    /** This code blocks works with OS X 10.5+ only. DO NOT DELETE IT for now. */
    CFStringRef urlStr = CFStringCreateWithCharacters(0,
   				reinterpret_cast<const UniChar *>(
                qurlStr.unicode()), qurlStr.size());
    CFURLRef urlRef = CFURLCreateWithFileSystemPath(NULL, urlStr, kCFURLPOSIXPathStyle, false);
    err = ExtAudioFileOpenURL(urlRef, &m_audioFile);
    CFRelease(urlStr);
    CFRelease(urlRef);

    /** TODO: Use FSRef for compatibility with 10.4 Tiger.
        Note that ExtAudioFileOpen() is deprecated above Tiger, so we must maintain
        both code paths if someone finishes this part of the code.
    FSRef fsRef;
    CFURLGetFSRef(reinterpret_cast<CFURLRef>(url.get()), &fsRef);
    err = ExtAudioFileOpen(&fsRef, &m_audioFile);
    */

    if (err != noErr) {
        qDebug() << "SSCA: Error opening file " << m_qFilename;
		return ERR;
	}

    // get the input file format
    CAStreamBasicDescription inputFormat;
    UInt32 size = sizeof(inputFormat);
    m_inputFormat = inputFormat;
    err = ExtAudioFileGetProperty(m_audioFile, kExtAudioFileProperty_FileDataFormat, &size, &inputFormat);
    if (err != noErr) {
        qDebug() << "SSCA: Error getting file format (" << m_qFilename << ")";
		return ERR;
	}

    //Debugging:
    //printf ("Source File format: "); inputFormat.Print();
    //printf ("Dest File format: "); outputFormat.Print();


	// create the output format
	CAStreamBasicDescription outputFormat;
    bzero(&outputFormat, sizeof(AudioStreamBasicDescription));
	outputFormat.mFormatID = kAudioFormatLinearPCM;
	outputFormat.mSampleRate = inputFormat.mSampleRate;
	outputFormat.mChannelsPerFrame = 2;
	outputFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger;

	/*
	switch(inputFormat.mBitsPerChannel) {
		case 16:
			outputFormat.mFormatFlags =  kAppleLosslessFormatFlag_16BitSourceData;
			break;
		case 20:
			outputFormat.mFormatFlags =  kAppleLosslessFormatFlag_20BitSourceData;
			break;
		case 24:
			outputFormat.mFormatFlags =  kAppleLosslessFormatFlag_24BitSourceData;
			break;
		case 32:
			outputFormat.mFormatFlags =  kAppleLosslessFormatFlag_32BitSourceData;
			break;
	}*/

    // get and set the client format - it should be lpcm
    CAStreamBasicDescription clientFormat = (inputFormat.mFormatID == kAudioFormatLinearPCM ? inputFormat : outputFormat);
	clientFormat.mBytesPerPacket = 4;
	clientFormat.mFramesPerPacket = 1;
	clientFormat.mBytesPerFrame = 4;
	clientFormat.mChannelsPerFrame = 2;
	clientFormat.mBitsPerChannel = 16;
	clientFormat.mReserved = 0;
	m_clientFormat = clientFormat;
    size = sizeof(clientFormat);

    err = ExtAudioFileSetProperty(m_audioFile, kExtAudioFileProperty_ClientDataFormat, size, &clientFormat);
    if (err != noErr) {
        qDebug() << "SSCA: Error setting file property (" << m_qFilename << ")";
		return ERR;
	}

	//Set m_iChannels and m_samples;
	m_iChannels = clientFormat.NumberChannels();

	//get the total length in frames of the audio file - copypasta: http://discussions.apple.com/thread.jspa?threadID=2364583&tstart=47
	UInt32		dataSize;
	SInt64		totalFrameCount;
	dataSize	= sizeof(totalFrameCount); //XXX: This looks sketchy to me - Albert
	err			= ExtAudioFileGetProperty(m_audioFile, kExtAudioFileProperty_FileLengthFrames, &dataSize, &totalFrameCount);
    if (err != noErr) {
        qDebug() << "SSCA: Error getting number of frames (" << m_qFilename << ")";
		return ERR;
	}

      //
      // WORKAROUND for bug in ExtFileAudio
      //

      AudioConverterRef acRef;
      UInt32 acrsize=sizeof(AudioConverterRef);
      err = ExtAudioFileGetProperty(m_audioFile, kExtAudioFileProperty_AudioConverter, &acrsize, &acRef);
      //_ThrowExceptionIfErr(@"kExtAudioFileProperty_AudioConverter", err);

      AudioConverterPrimeInfo primeInfo;
      UInt32 piSize=sizeof(AudioConverterPrimeInfo);
      memset(&primeInfo, 0, piSize);
      err = AudioConverterGetProperty(acRef, kAudioConverterPrimeInfo, &piSize, &primeInfo);
      if(err != kAudioConverterErr_PropertyNotSupported) // Only if decompressing
      {
         //_ThrowExceptionIfErr(@"kAudioConverterPrimeInfo", err);

         m_headerFrames=primeInfo.leadingFrames;
      }

	m_samples = (totalFrameCount/*-m_headerFrames*/)*m_iChannels;
	m_iDuration = m_samples / (inputFormat.mSampleRate * m_iChannels);
	m_iSampleRate = inputFormat.mSampleRate;
	qDebug() << m_samples << totalFrameCount << m_iChannels;

	//Seek to position 0, which forces us to skip over all the header frames.
	//This makes sure we're ready to just let the Analyser rip and it'll
	//get the number of samples it expects (ie. no header frames).
	seek(0);

    return OK;
}

long SoundSourceCoreAudio::seek(long filepos) {
    // important division here, filepos is in audio samples (i.e. shorts)
    // but libflac expects a number in time samples. I _think_ this should
    // be hard-coded at two because *2 is the assumption the caller makes
    // -- bkgood
    OSStatus err = noErr;
    SInt64 segmentStart = filepos / 2;

      err = ExtAudioFileSeek(m_audioFile, (SInt64)segmentStart+m_headerFrames);
      //_ThrowExceptionIfErr(@"ExtAudioFileSeek", err);
	//qDebug() << "SSCA: Seeking to" << segmentStart;

	//err = ExtAudioFileSeek(m_audioFile, filepos / 2);
    if (err != noErr) {
        qDebug() << "SSCA: Error seeking to" << filepos << " (file " << m_qFilename << ")";// << GetMacOSStatusErrorString(err) << GetMacOSStatusCommentString(err);
	}
    return filepos;
}

unsigned int SoundSourceCoreAudio::read(unsigned long size, const SAMPLE *destination) {
    //if (!m_decoder) return 0;
    OSStatus err;
    SAMPLE *destBuffer(const_cast<SAMPLE*>(destination));
    UInt32 numFrames = 0;//(size / 2); /// m_inputFormat.mBytesPerFrame);
    unsigned int totalFramesToRead = size/2;
    unsigned int numFramesRead = 0;
    unsigned int numFramesToRead = totalFramesToRead;

    while (numFramesRead < totalFramesToRead) { //FIXME: Hardcoded 2
    	numFramesToRead = totalFramesToRead - numFramesRead;

		AudioBufferList fillBufList;
		fillBufList.mNumberBuffers = 1; //Decode a single track?
		fillBufList.mBuffers[0].mNumberChannels = m_inputFormat.mChannelsPerFrame;
		fillBufList.mBuffers[0].mDataByteSize = math_min(1024, numFramesToRead*4);//numFramesToRead*sizeof(*destBuffer); // 2 = num bytes per SAMPLE
		fillBufList.mBuffers[0].mData = (void*)(&destBuffer[numFramesRead*2]);

			// client format is always linear PCM - so here we determine how many frames of lpcm
			// we can read/write given our buffer size
		numFrames = numFramesToRead; //This silly variable acts as both a parameter and return value.
		err = ExtAudioFileRead (m_audioFile, &numFrames, &fillBufList);
		//The actual number of frames read also comes back in numFrames.
		//(It's both a parameter to a function and a return value. wat apple?)
		//XThrowIfError (err, "ExtAudioFileRead");
		if (!numFrames) {
				// this is our termination condition
			break;
		}
		numFramesRead += numFrames;
    }
    return numFramesRead*2;
}

inline unsigned long SoundSourceCoreAudio::length() {
    return m_samples;
}

int SoundSourceCoreAudio::parseHeader() {
    if (getFilename().endsWith(".m4a"))
        setType("m4a");
    else if (getFilename().endsWith(".mp3"))
        setType("mp3");
    else if (getFilename().endsWith(".mp2"))
        setType("mp2");

    bool result = false;

    if (getType() == "m4a") {
        // No need for toLocal8Bit on Windows since CoreAudio is OS X only.
        TagLib::MP4::File f(getFilename().toUtf8().constData());
        result = processTaglibFile(f);
        TagLib::MP4::Tag* tag = f.tag();
        if (tag) {
            processMP4Tag(tag);
        }
    } else if (getType() == "mp3") {
        // No need for toLocal8Bit on Windows since CoreAudio is OS X only.
        TagLib::MPEG::File f(getFilename().toUtf8().constData());

        // Takes care of all the default metadata
        result = processTaglibFile(f);

        // Now look for MP3 specific metadata (e.g. BPM)
        TagLib::ID3v2::Tag* id3v2 = f.ID3v2Tag();
        if (id3v2) {
            processID3v2Tag(id3v2);
        }

        TagLib::APE::Tag *ape = f.APETag();
        if (ape) {
            processAPETag(ape);
        }
    } else if (getType() == "mp2") {
        //TODO: MP2 metadata. Does anyone use mp2 files anymore?
        //      Feels like 1995 again...
    }

    if (result == ERR) {
        qWarning() << "Error parsing header of file" << m_qFilename;
    }
    return result ? OK : ERR;
}


// static
QList<QString> SoundSourceCoreAudio::supportedFileExtensions() {
    QList<QString> list;
    list.push_back("m4a");
    list.push_back("mp3");
    list.push_back("mp2");
    //Can add mp3, mp2, ac3, and others here if you want.
    //See:
    //  http://developer.apple.com/library/mac/documentation/MusicAudio/Reference/AudioFileConvertRef/Reference/reference.html#//apple_ref/doc/c_ref/AudioFileTypeID

    //XXX: ... but make sure you implement handling for any new format in ParseHeader!!!!!! -- asantoni
    return list;
}
