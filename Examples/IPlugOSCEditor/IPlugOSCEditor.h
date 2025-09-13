#pragma once

#include "IPlugOSC.h"
#include "IPlug_include_in_plug_hdr.h"

const int kNumPresets = 1;

enum EParams
{
  kGain = 0,
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagGain = 0,
  kCtrlTagSendIP,
  kCtrlTagSendPort,
  kCtrlTagWebView,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

#if IPLUG_SEPARATE_OSC_STATE
class IPlugOSCEditor final : public Plugin, public OSCManager, public OSCReceiver, public OSCSender
#else
class IPlugOSCEditor final : public Plugin, public OSCReceiver, public OSCSender
#endif
{
public:
  IPlugOSCEditor(const InstanceInfo& info);

  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;

  void OnOSCMessage(OscMessageRead& msg) override;
};
