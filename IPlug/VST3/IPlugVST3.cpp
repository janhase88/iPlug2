/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include <cstdio>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "IPlugVST3.h"
#include "IPlugLogger.h"

using namespace iplug;
using namespace Steinberg;
using namespace Vst;

#include "IPlugVST3_Parameter.h"

#pragma mark - IPlugVST3 Constructor/Destructor

IPlugVST3::IPlugVST3(const InstanceInfo& info, const Config& config)
  : IPlugAPIBase(config, kAPIVST3)
  , IPlugVST3ProcessorBase(config, *this)
  , IPlugVST3ControllerBase(parameters)
  , mView(nullptr)
{
  CreateTimer();
}

IPlugVST3::~IPlugVST3() {}

#pragma mark AudioEffect overrides

Steinberg::uint32 PLUGIN_API IPlugVST3::getTailSamples() { return GetTailIsInfinite() ? kInfiniteTail : GetTailSize(); }

tresult PLUGIN_API IPlugVST3::initialize(FUnknown* context)
{
  TRACEF(GetLogFile());

  if (SingleComponentEffect::initialize(context) == kResultOk)
  {
    IPlugVST3ProcessorBase::Initialize(this);
    IPlugVST3ControllerBase::Initialize(this, IsInstrument(), DoesMIDIIn());

    IPlugVST3GetHost(this, context);
    OnHostIdentified();
    OnParamReset(kReset);

    return kResultOk;
  }

  return kResultFalse;
}

tresult PLUGIN_API IPlugVST3::terminate()
{
  TRACEF(GetLogFile());

  return SingleComponentEffect::terminate();
}

tresult PLUGIN_API IPlugVST3::setBusArrangements(SpeakerArrangement* pInputBusArrangements, int32 numInBuses, SpeakerArrangement* pOutputBusArrangements, int32 numOutBuses)
{
  TRACEF(GetLogFile());

  return IPlugVST3ProcessorBase::SetBusArrangements(this, pInputBusArrangements, numInBuses, pOutputBusArrangements, numOutBuses) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API IPlugVST3::setActive(TBool state)
{
  TRACEF(GetLogFile());

  OnActivate((bool)state);
  return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API IPlugVST3::setupProcessing(ProcessSetup& newSetup)
{
  TRACEF(GetLogFile());

  return SetupProcessing(newSetup, processSetup) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API IPlugVST3::setProcessing(TBool state)
{
  Trace(GetLogFile(), TRACELOC, "inst=%p state: %i", this, state);

  return SetProcessing((bool)state) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API IPlugVST3::process(ProcessData& data)
{
  TRACEF(GetLogFile());

  Process(data, processSetup, audioInputs, audioOutputs, mMidiMsgsFromEditor, mMidiMsgsFromProcessor, mSysExDataFromEditor, mSysexBuf);
  return kResultOk;
}

tresult PLUGIN_API IPlugVST3::canProcessSampleSize(int32 symbolicSampleSize) { return CanProcessSampleSize(symbolicSampleSize) ? kResultTrue : kResultFalse; }

tresult PLUGIN_API IPlugVST3::setState(IBStream* pState)
{
  TRACEF(GetLogFile());

  return IPlugVST3State::SetState(this, pState) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API IPlugVST3::getState(IBStream* pState)
{
  TRACEF(GetLogFile());

  return IPlugVST3State::GetState(this, pState) ? kResultOk : kResultFalse;
}

#pragma mark IEditController overrides
ParamValue PLUGIN_API IPlugVST3::getParamNormalized(ParamID tag) { return IPlugVST3ControllerBase::GetParamNormalized(tag); }

tresult PLUGIN_API IPlugVST3::setParamNormalized(ParamID tag, ParamValue value)
{
  if (IPlugVST3ControllerBase::SetParamNormalized(this, tag, value))
    return kResultTrue;
  else
    return kResultFalse;
}

IPlugView* PLUGIN_API IPlugVST3::createView(const char* name)
{
  if (HasUI() && name && strcmp(name, "editor") == 0)
  {
    mView = new ViewType(*this);
    return mView;
  }

  return nullptr;
}

tresult PLUGIN_API IPlugVST3::setEditorState(IBStream* pState)
{
  // Currently nothing to do here
  return kResultOk;
}

tresult PLUGIN_API IPlugVST3::getEditorState(IBStream* pState)
{
  // Currently nothing to do here
  return kResultOk;
}

tresult PLUGIN_API IPlugVST3::setComponentState(IBStream* pState)
{
  // We get the state through setState so do nothing here
  return kResultOk;
}

#pragma mark IMidiMapping overrides

tresult PLUGIN_API IPlugVST3::getMidiControllerAssignment(int32 busIndex, int16 midiChannel, CtrlNumber midiCCNumber, ParamID& tag)
{
  if (busIndex == 0 && midiChannel < VST3_NUM_CC_CHANS)
  {
    tag = kMIDICCParamStartIdx + (midiChannel * kCountCtrlNumber) + midiCCNumber;
    return kResultTrue;
  }

  return kResultFalse;
}

#pragma mark IInfoListener overrides

Steinberg::tresult PLUGIN_API IPlugVST3::setChannelContextInfos(Steinberg::Vst::IAttributeList* pList) { return IPlugVST3ControllerBase::SetChannelContextInfos(pList) ? kResultTrue : kResultFalse; }

#pragma mark IPlugAPIBase overrides

void IPlugVST3::BeginInformHostOfParamChange(int idx)
{
  Trace(GetLogFile(), TRACELOC, "inst=%p %d", mInstanceID, idx);
  beginEdit(idx);
}

void IPlugVST3::InformHostOfParamChange(int idx, double normalizedValue)
{
  Trace(GetLogFile(), TRACELOC, "inst=%p %d:%f", mInstanceID, idx, normalizedValue);
  performEdit(idx, normalizedValue);
}

void IPlugVST3::EndInformHostOfParamChange(int idx)
{
  Trace(GetLogFile(), TRACELOC, "inst=%p %d", mInstanceID, idx);
  endEdit(idx);
}

void IPlugVST3::InformHostOfParameterDetailsChange()
{
  FUnknownPtr<IComponentHandler> handler(componentHandler);
  handler->restartComponent(kParamTitlesChanged);
}

bool IPlugVST3::EditorResize(int viewWidth, int viewHeight)
{
  if (HasUI())
  {
    if (viewWidth != GetEditorWidth() || viewHeight != GetEditorHeight())
      mView->Resize(viewWidth, viewHeight);

    SetEditorSize(viewWidth, viewHeight);
  }

  return true;
}

#pragma mark IEditorDelegate overrides

void IPlugVST3::DirtyParametersFromUI()
{
  for (int i = 0; i < NParams(); i++)
    IPlugVST3ControllerBase::SetVST3ParamNormalized(i, GetParam(i)->GetNormalized());

  startGroupEdit();
  IPlugAPIBase::DirtyParametersFromUI();
  finishGroupEdit();
}

void IPlugVST3::SendParameterValueFromUI(int paramIdx, double normalisedValue)
{
  IPlugVST3ControllerBase::SetVST3ParamNormalized(paramIdx, normalisedValue);
  IPlugAPIBase::SendParameterValueFromUI(paramIdx, normalisedValue);
}

void IPlugVST3::SetLatency(int latency)
{
  // N.B. set the latency even if the handler is not yet set

  IPlugProcessor::SetLatency(latency);

  if (componentHandler)
  {
    FUnknownPtr<IComponentHandler> handler(componentHandler);

    if (handler)
    {
      handler->restartComponent(kLatencyChanged);
    }
  }
}
