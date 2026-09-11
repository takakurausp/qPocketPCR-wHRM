
#include "Preferences.h"
#include "Parsing.h"
#include "USB_DRIVE.h"

ProtocolType pcrProtocol;

float getFloatFromString(String InputString)
{
  // Extract the substring containing the float number
  String floatString = InputString;
  floatString.trim();
  // Convert the extracted substring to a float
  float extractedFloat = floatString.toFloat();

  return extractedFloat;
}

float getSecondFloatFromString(String InputString)
{
  int idx = 0;

  while (idx < InputString.length() && isdigit(InputString.charAt(idx)))
    idx++;
  while (idx < InputString.length() && !isdigit(InputString.charAt(idx)))
    idx++;

  // Extract the substring containing the float number
  String floatString = InputString.substring(idx, InputString.length());
  ;
  floatString.trim();

  // Convert the extracted substring to a float
  float extractedFloat = floatString.toFloat();

  return extractedFloat;
}

String getStringInBrackets(String InputString)
{
  int startPos = InputString.indexOf("(");
  int endPos = InputString.indexOf(")");
  String floatString = "";
  if ((startPos != -1) && (endPos != -1))
  {
    // Extract the substring containing the float number
    floatString = InputString.substring(startPos + 1, endPos);
    floatString.trim();
    // Convert the extracted substring to a float
  }
  return floatString;
}

boolean isFarenheit(String InputString)
{
  return (InputString.indexOf("f") != -1) || (InputString.indexOf("F") != -1);
}

boolean isMinutes(String InputString)
{
  return (InputString.indexOf("m") != -1) || (InputString.indexOf("M") != -1);
}

String getStringFromStream(String InputString, String InputStream)

{
  String floatString = "";
  // Find the position of the float number in the string
  int startPos = InputStream.indexOf(InputString);
  if (startPos != -1)
  {
    floatString = InputStream.substring(startPos + InputString.length());
    int endPos = floatString.indexOf("\n");
    floatString = floatString.substring(0, endPos);
    floatString.trim();
  }
  return floatString;
}

void getSteps(String InputStream)

{
  int startStep = InputStream.indexOf("STEP");
  int endStep = 0;
  int stepCount = 0;

  String floatString;

  while ((startStep != -1) && (stepCount < MAX_STEPS))
  {

    floatString = InputStream.substring(startStep + 4);
    endStep = floatString.indexOf("STEP");

    if (endStep != -1)
    {
      floatString = floatString.substring(0, endStep);
      startStep = startStep + 4 + endStep;
    }
    else
    {
      startStep = endStep;
    }

    pcrProtocol.steps[stepCount].name = getStringFromStream(":", floatString);
    // Serial.println( (int)getFloatFromString(getStringFromStream("STEP",floatString)));

    pcrProtocol.steps[stepCount].temperature = getFloatFromString(getStringFromStream("TEMPERATURE:", floatString));
    if (isFarenheit(getStringFromStream("TEMPERATURE:", floatString)))
      pcrProtocol.steps[stepCount].temperature = (5.0 / 9.0) * (pcrProtocol.steps[stepCount].temperature - 32.0);
    ;
    pcrProtocol.steps[stepCount].duration = getFloatFromString(getStringFromStream("DURATION:", floatString));
    if (pcrProtocol.steps[stepCount].temperature > 99)
      pcrProtocol.steps[stepCount].temperature = 99;
    if (pcrProtocol.steps[stepCount].temperature < 20)
      pcrProtocol.steps[stepCount].temperature = 20;

    if (isMinutes(getStringFromStream("DURATION:", floatString)))
      pcrProtocol.steps[stepCount].duration = pcrProtocol.steps[stepCount].duration * 60;
    if ((getStringFromStream("CAPTURE", floatString)) != "")
      pcrProtocol.steps[stepCount].capture = true;
    else
      pcrProtocol.steps[stepCount].capture = false;

    // Serial.println( pcrProtocol.steps[stepCount].name);
    // Serial.println( pcrProtocol.steps[stepCount].temperature);
    // Serial.println( pcrProtocol.steps[stepCount].duration);

    stepCount++;
  }

  pcrProtocol.stepCount = stepCount;
}

void parseConfig(String InputStream)
{

  pcrProtocol.stepCount = 0;
  pcrProtocol.melt = false;
  pcrProtocol.meltPoints = 0;

  if (InputStream.length() > 0)
  {
    pcrProtocol.name = getStringFromStream("NAME:", InputStream);
    pcrProtocol.date = getStringFromStream("DATE:", InputStream);
    pcrProtocol.repeatStart = getFloatFromString(getStringFromStream("REPEAT:", InputStream));
    pcrProtocol.repeatEnd = getSecondFloatFromString(getStringFromStream("REPEAT:", InputStream));
    if (pcrProtocol.repeatEnd <= pcrProtocol.repeatStart)
      pcrProtocol.repeatEnd = pcrProtocol.repeatStart;
    pcrProtocol.cycleCount = getFloatFromString(getStringFromStream("CYCLES:", InputStream));

    getSteps(InputStream);

    // ---- MELT (High Resolution Melting) ----
    // A PROTOCOL.TXT may append a MELT block after the normal steps to run a
    // fine temperature ramp with fluorescence capture at every point, e.g.:
    //   MELT FROM: 65.0
    //   MELT TO: 95.0
    //   MELT INC: 0.2
    //   MELT HOLD: 2
    // Supported run forms:
    //   amplification only       -> no MELT lines
    //   amplification then melt  -> steps + MELT lines
    //   melt only                -> only MELT lines (no STEP lines)
    // The MELT steps are expanded into steps[] AFTER the repeat region, so the
    // existing state machine runs them exactly once, when the PCR cycles end.
    if (InputStream.indexOf("MELT") != -1)
    {
      pcrProtocol.melt = true;

      String meltFromStr = getStringFromStream("MELT FROM:", InputStream);
      String meltToStr   = getStringFromStream("MELT TO:", InputStream);
      String meltIncStr  = getStringFromStream("MELT INC:", InputStream);
      String meltHoldStr = getStringFromStream("MELT HOLD:", InputStream);

      if (meltFromStr.length() > 0) pcrProtocol.meltFrom = getFloatFromString(meltFromStr);
      if (meltToStr.length()   > 0) pcrProtocol.meltTo   = getFloatFromString(meltToStr);
      if (meltIncStr.length()  > 0) pcrProtocol.meltInc  = getFloatFromString(meltIncStr);
      if (meltHoldStr.length() > 0) pcrProtocol.meltHold = (int)getFloatFromString(meltHoldStr);

      if (pcrProtocol.meltInc <= 0.0f) pcrProtocol.meltInc = 0.2f;
      if (pcrProtocol.meltHold < 1)    pcrProtocol.meltHold = 1;
      if (pcrProtocol.meltFrom < 20)   pcrProtocol.meltFrom = 20;
      if (pcrProtocol.meltTo   > 99)   pcrProtocol.meltTo   = 99;
      if (pcrProtocol.meltFrom >= pcrProtocol.meltTo)
      {
        Serial.println("MELT: invalid range, using 65.0 - 95.0");
        pcrProtocol.meltFrom = 65.0f;
        pcrProtocol.meltTo   = 95.0f;
      }

      // Point count via integer-safe rounding (float error stays below 0.5)
      int requestedPoints = (int)((pcrProtocol.meltTo - pcrProtocol.meltFrom) / pcrProtocol.meltInc + 0.5f) + 1;
      if (requestedPoints < 1) requestedPoints = 1;

      int stepIndex = pcrProtocol.stepCount;
      for (int i = 0; (i < requestedPoints) && (stepIndex < MAX_STEPS); i++, stepIndex++)
      {
        pcrProtocol.steps[stepIndex].name        = String("Melt ") + String(pcrProtocol.meltFrom + (float)i * pcrProtocol.meltInc, 1);
        pcrProtocol.steps[stepIndex].temperature = pcrProtocol.meltFrom + (float)i * pcrProtocol.meltInc;
        pcrProtocol.steps[stepIndex].duration    = (float)pcrProtocol.meltHold;
        pcrProtocol.steps[stepIndex].capture     = true;
      }

      pcrProtocol.meltPoints = stepIndex - pcrProtocol.stepCount;
      pcrProtocol.stepCount  = stepIndex;

      Serial.print("MELT: expanding to ");
      Serial.print(pcrProtocol.meltPoints);
      Serial.println(" capture steps");
      if (pcrProtocol.meltPoints < requestedPoints)
        Serial.println("MELT: warning - steps[] full, ramp truncated");
    }

    /*  Serial.print("Name");
    // Serial.println(pcrProtocol.name);

     Serial.print("Date");
     Serial.println(pcrProtocol.date);

     Serial.print("repeatStart");
     Serial.println(pcrProtocol.repeatStart);

     Serial.print("repeatEnd");
     Serial.println(pcrProtocol.repeatEnd);

     Serial.println(pcrProtocol.steps[1].name);
     Serial.println(pcrProtocol.steps[1].temperature);
     Serial.println(pcrProtocol.steps[1].duration);
   */
  }
}
void loadProtocol()
{
  // SPIFFS に PROTOCOL.TXT があれば優先して読み込む（Web アップロード対応）
  File protoFile = SPIFFS.open("/PROTOCOL.TXT", FILE_READ);
  if (protoFile) {
    String myConfig = protoFile.readString();
    protoFile.close();
    Serial.printf("loadProtocol: from SPIFFS (%d bytes)\n", myConfig.length());
    parseConfig(myConfig);
    return;
  }

  // なければ USB ディスクイメージから読み込む
  String myConfig = getConfig();
  Serial.printf("loadProtocol: from USB disk (%d bytes)\n", myConfig.length());
  parseConfig(myConfig);
}
