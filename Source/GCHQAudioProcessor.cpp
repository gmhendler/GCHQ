/*
  ==============================================================================

    This file was auto-generated by the Introjucer!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/
#include "GCHQAudioProcessor.h"
#include "GCHQEditor.h"
#include "FIRCoeffecients.h"
#include "FFTW.h"

//==============================================================================
GCHQAudioProcessor::GCHQAudioProcessor()
  : isPrepared(false), currentProgram(0), gainIn(1.f), gainOut(1.f)
{
  
}

GCHQAudioProcessor::~GCHQAudioProcessor()
{
}

//==============================================================================
const String GCHQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

int GCHQAudioProcessor::getNumParameters()
{
    return kMaxParameters;
}

float GCHQAudioProcessor::getParameter (int index)
{
  float value;
  switch (index) {
  case kProgramParameter:
    value = programToParameter(currentProgram);
    break;
  case kGainInParameter:
    value = gainToParameter(gainIn);
    break;
  case kGainOutParameter:
    value = gainToParameter(gainOut);
    break;
  default:
    value = 0.f;
  }
  return value;
}

void GCHQAudioProcessor::setParameter (int index, float value)
{
  switch (index) {
  case kProgramParameter:
    {
      int newProgram = parameterToProgram(value);
      newProgram = jmin( jmax(0, newProgram), kFIRPrograms.numPrograms );
      if ( newProgram != currentProgram ) {
	if ( isPrepared == false ) {
	/* if the plug-in isn't running then just change the program */
	  currentProgram = newProgram;
	} else {
	  /* TODO: this is really brute force here. 
	     There is definitely a way to do this more elegantly */
	  suspendProcessing(true);
	  
	  /* load the new program */
	  currentProgram = newProgram;
	  releaseResources();
	  prepareToPlay(getSampleRate(), getBlockSize());
	  
	  suspendProcessing(false);
	}
      }
    }
    break;
  case kGainInParameter:
    gainIn = parameterToGain(value);
    break;
  case kGainOutParameter:
    gainOut = parameterToGain(value);
    break;
  default:
    break;
  }
}

const String GCHQAudioProcessor::getParameterName (int index)
{
  String value;
  switch (index) {
  case kProgramParameter:
    value = "Program";
    break;
  case kGainInParameter:
    value = "In Gain";
    break;
  case kGainOutParameter:
    value = "Out Gain";
    break;
  default:
    value = "";
  }
  return value;
}

const String GCHQAudioProcessor::getParameterText (int index)
{
  String value;
  switch (index) {
  case kProgramParameter:
    value = kFIRPrograms.programs[currentProgram].programName;
    break;
  case kGainInParameter:
    value = String(Decibels::gainToDecibels(gainIn));
    break;
  case kGainOutParameter:
    value = String(Decibels::gainToDecibels(gainOut));
    break;
  default:
    value = "";
    break;
  }
  return value;
}

int GCHQAudioProcessor::getParameterNumSteps (int index) {
  int value;
  switch (index) {
  case kProgramParameter:
    value = kFIRPrograms.numPrograms;
    break;
  default:
    value = 0;
  }
  return value;
}

String GCHQAudioProcessor::getParameterLabel (int index) const {
  String value;
  switch (index) {
  case kGainInParameter:
  case kGainOutParameter:
    value = "dB";
    break;
  default:
    value = "";
  }
  return value;
}

float GCHQAudioProcessor::getParameterDefaultValue (int index) {
  float value;
  switch (index) {
  case kProgramParameter:
    value = 0.f;
    break;
  case kGainInParameter:
  case kGainOutParameter:
    value = gainToParameter(1.f);
    break;
  default:
    value = 0.f;
  }
  return value;
}

const String GCHQAudioProcessor::getInputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

const String GCHQAudioProcessor::getOutputChannelName (int channelIndex) const
{
    return String (channelIndex + 1);
}

bool GCHQAudioProcessor::isInputChannelStereoPair (int index) const
{
    return true;
}

bool GCHQAudioProcessor::isOutputChannelStereoPair (int index) const
{
    return true;
}

bool GCHQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool GCHQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool GCHQAudioProcessor::silenceInProducesSilenceOut() const
{
    return false;
}

double GCHQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int GCHQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int GCHQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void GCHQAudioProcessor::setCurrentProgram (int index)
{
}

const String GCHQAudioProcessor::getProgramName (int index)
{
    return String();
}

void GCHQAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void GCHQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
  int rawNumberOfCoeffecients=kFIRPrograms.programs[currentProgram].numberOfCoeffecients;
  unsigned int firSize = (rawNumberOfCoeffecients-1)<<1;
  firSize = round((sampleRate/44100.)*static_cast<double>(firSize));
  
  /* calculate fftOrder and blockSize for this samplerate */
  fftOrder = static_cast<int>(ceil(log2(firSize)));
  blockSize = (1<<fftOrder) - firSize + 1;
  while ( blockSize < 8192 ) {
    fftOrder++;
    blockSize = (1<<fftOrder) - firSize + 1;
  }

  /* dont introduce latencies more than one second */
  if ( blockSize >= round(sampleRate) ) {
    blockSize = round(sampleRate);
  }

  setLatencySamples(blockSize);

  // Re-calculate the FIR filter
  prepareFIRFilter(sampleRate, 1 << fftOrder);

  // (Re-)allocate the FFTs
  kernels.clear();
  for ( int i=0; i < getNumInputChannels(); ++i ) {
    kernels.add(new GCHQKernel(blockSize, firCoeffs));
  }

  samplesSinceIndicator = 0;
  isPrepared = true;
}

void GCHQAudioProcessor::releaseResources()
{
  isPrepared = false;
  kernels.clear();
  setIndicator( false );
}

void GCHQAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
  bool overflow = false;
  for (int i = getNumInputChannels(); i < getNumOutputChannels(); ++i)
    {
      buffer.clear (i, 0, buffer.getNumSamples());
    }
  
  for (int channel = 0; channel < getNumInputChannels(); ++channel)
    {
      float* channelData = buffer.getWritePointer (channel);
      
      if ( overflow == false ) 
	{
	  /* check for overflow */
	  Range<float> minmax = 
	    FloatVectorOperations::findMinAndMax(channelData, buffer.getNumSamples());
	  float absValue = jmax(fabs(minmax.getStart()), fabs(minmax.getEnd()));
	  if ( ( absValue * gainIn ) > 0.99f ) 
	    {
	      overflow = true;
	    }
	}
      
      kernels[channel]->process(channelData, buffer.getNumSamples(), gainIn, gainOut);
    }
  
  /* set indicator flag appropriately */
  if ( overflow == true ) {
    samplesSinceIndicator = 0;
    setIndicator(true);
  } else {
    samplesSinceIndicator += buffer.getNumSamples();
    if ( samplesSinceIndicator >= round(getSampleRate()) ) {
      setIndicator(false);
    }
  }
}

void GCHQAudioProcessor::prepareFIRFilter(double sampleRate, unsigned int segmentSize)
{
  // Convert raw impulse response data to time-domain and do any necessary
  // padding taking care of any sample rate conversions. Then convert
  // back to frequency domain with paddings in place.
  
  // raw fir size in time-domain as stored in the plug-in
  unsigned int rawNumComplex = kFIRPrograms.programs[currentProgram].numberOfCoeffecients;

  // number of real data points represented by the stored coeffecients
  unsigned int rawfirSize = (rawNumComplex-1) << 1;
  
  // expand to sampleRate ratio
  unsigned int firSize = 
    round((sampleRate/44100.)*static_cast<double>(rawfirSize));
  // and re-calculate number of complex coeffecients
  unsigned int numComplex = (firSize>>1)+1;
  {
    FFTW inverseTrans(firSize, false); /* this will zero-out all buffers */

    FFT::Complex* rawFIRCoeffecients = 
      (FFT::Complex*)kFIRPrograms.programs[currentProgram].data;

    /* fill with raw coeffecients */
    {
      int n = jmin(rawNumComplex,numComplex);
      for ( int i=0; i<n; ++i ) {	
	inverseTrans.getComplexBuffer()[i] = rawFIRCoeffecients[i];
      }
    }
    
    inverseTrans.execute();
    /* now the fir is in time-domain with the correct sample-rate, however 
       we need to transform it back to the frequency-domain with necessary
       padding to match the segmentSize */
    {
      FFTW forwardTrans(segmentSize, true); /* this will clear all buffers */

      /* zero-out and copy */
      FloatVectorOperations::copy( forwardTrans.getRealBuffer(),
				   inverseTrans.getRealBuffer(), firSize );
      forwardTrans.execute();
      /* now just copy the FIR filter to a new buffer ready for usage */
      firCoeffs.resize(forwardTrans.getComplexSize());
      /* sample-rate conversion incurrs the following divider */
      float divider = 1.f/sqrt(static_cast<float>(segmentSize)*static_cast<float>(firSize));
      /* the forward and backward transformation in the plug-in needs to be normalised as well */
      divider /= static_cast<float>(segmentSize);
      {
	FFT::Complex* dst = firCoeffs.getRawDataPointer();
	for ( int i=0; i<forwardTrans.getComplexSize(); ++i ) {
	  dst[i].r = forwardTrans.getComplexBuffer()[i].r * divider;
	  dst[i].i = forwardTrans.getComplexBuffer()[i].i * divider;
	}
      }
    }
  }
}

//==============================================================================
int GCHQAudioProcessor::parameterToProgram(float parameter) {
  if ( kFIRPrograms.numPrograms < 2 ) {
    return 0;
  }

  int newValue = round(parameter*static_cast<float>(kFIRPrograms.numPrograms-1));
  return jmax(0, jmin(newValue, kFIRPrograms.numPrograms-1) );
}
 
float GCHQAudioProcessor::programToParameter(int program) {
  program = jmax(0, jmin(program, kFIRPrograms.numPrograms-1));
  if ( kFIRPrograms.numPrograms < 2 ) {
    return 0.f;
  }

  return static_cast<float>(program)/
    static_cast<float>(kFIRPrograms.numPrograms-1);
}

//==============================================================================
bool GCHQAudioProcessor::hasEditor() const
{
  return true;
}

AudioProcessorEditor* GCHQAudioProcessor::createEditor()
{
    return new GCHQEditor (*this);
}

void GCHQAudioProcessor::setIndicator( bool indicator ) {
  GCHQEditor* editor;
  if ( ( editor = dynamic_cast<GCHQEditor*>(getActiveEditor()) ) != nullptr ) {
    editor->setIndicator(indicator);
  }
}
//==============================================================================
void GCHQAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void GCHQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GCHQAudioProcessor();
}
