
#include "Preferences.h"
#define MAX_STEPS 400

typedef struct StepType
{
  String name;
  float temperature;
  float duration;
  boolean capture;
};

typedef struct ProtocolType
{

  StepType steps[MAX_STEPS];
  String name = "undefined";
  String date = "undefined";
  int stepCount = 0;
  int cycleCount = 0;
  int repeatStart = 0;
  int repeatEnd = 0;
  // MELT (High Resolution Melting) settings, set by the MELT FROM/TO/INC/HOLD
  // lines in PROTOCOL.TXT. MELT steps are expanded into steps[] after parsing.
  bool melt = false;
  float meltFrom = 65.0;
  float meltTo = 95.0;
  float meltInc = 0.2;
  int meltHold = 2;      // seconds per melt point
  int meltPoints = 0;    // number of expanded melt capture steps
};

extern ProtocolType pcrProtocol;

void loadProtocol();
