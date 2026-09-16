
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

    // If there are no STEP lines but a MELT block is present, this is an
    // HRM-only protocol. The state machine below expects at least one step to
    // run; set repeatStart/repeatEnd so the (melt-expanded) steps execute
    // exactly once without cycling.
    if (pcrProtocol.stepCount == 0 && InputStream.indexOf("MELT") != -1)
    {
      pcrProtocol.repeatStart = 1;
      pcrProtocol.repeatEnd   = 1;
    }

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

// ================= Named protocol library =================
// Protocols are stored as text files PROTO_01.txt, PROTO_02.txt ... on SPIFFS.
// PROTOLIST.TXT keeps "id:name" lines so the builder UI can list and reload them.

static String protoFileName(int id)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "/PROTO_%02d.txt", id);
  return String(buf);
}

// Find the lowest free protocol id in 1..99 (reuses ids freed by deletion so
// the limited slot space does not leak over time). Returns -1 when all used.
static int firstFreeProtoId()
{
  bool used[100] = {false};

  File f = SPIFFS.open("/PROTOLIST.TXT", FILE_READ);
  if (f) {
    String line;
    while (f.available()) {
      line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int colon = line.indexOf(':');
      if (colon > 0) {
        int id = line.substring(0, colon).toInt();
        if (id >= 1 && id < 100) used[id] = true;
      }
    }
    f.close();
  }

  // Treat orphaned files (present on flash but missing from the list) as used.
  for (int i = 1; i < 100; i++) {
    if (!used[i] && SPIFFS.exists(protoFileName(i))) used[i] = true;
  }

  for (int i = 1; i < 100; i++) {
    if (!used[i]) return i;
  }
  return -1;
}

void saveNamedProtocol(String name, String text)
{
  name.trim();
  if (name.length() == 0) name = "Unnamed";

  int nextId = firstFreeProtoId();
  if (nextId < 0) {
    Serial.println("saveNamedProtocol: no free protocol slot (max 99)");
    return;
  }

  // Write the protocol text file.
  File pf = SPIFFS.open(protoFileName(nextId), FILE_WRITE);
  if (!pf) {
    Serial.println("saveNamedProtocol: failed to open protocol file for writing");
    return;
  }
  pf.print(text);
  pf.close();

  // Update the management list. Overwrite an existing line, else append.
  File lf = SPIFFS.open("/PROTOLIST.TXT", FILE_READ);
  String content = "";
  if (lf) {
    content = lf.readString();
    lf.close();
  }
  String newLine = String(nextId) + ":" + name;
  bool replaced = false;
  String out = "";
  String line;
  // Split on both \n and \r\n.
  while (content.length() > 0) {
    int nl = content.indexOf('\n');
    String l;
    if (nl >= 0) { l = content.substring(0, nl); content = content.substring(nl + 1); }
    else { l = content; content = ""; }
    // strip trailing \r
    if (l.length() > 0 && l[l.length() - 1] == '\r') l.remove(l.length() - 1);
    if (!replaced) {
      int colon = l.indexOf(':');
      if (colon > 0 && l.substring(0, colon).toInt() == nextId) {
        out += newLine + "\n";
        replaced = true;
        continue;
      }
    }
    out += l + "\n";
  }
  if (!replaced) out += newLine + "\n";

  File lf2 = SPIFFS.open("/PROTOLIST.TXT", FILE_WRITE);
  if (lf2) { lf2.print(out); lf2.close(); }

  Serial.printf("saveNamedProtocol: saved id=%d \"%s\" (%d bytes)\n", nextId, name.c_str(), text.length());
}

String listProtocols()
{
  String out = "";
  File f = SPIFFS.open("/PROTOLIST.TXT", FILE_READ);
  if (!f) return out;
  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int colon = line.indexOf(':');
    if (colon > 0) {
      String id = line.substring(0, colon);
      String nm = line.substring(colon + 1);
      out += id + "|" + nm + "\n";
    }
  }
  f.close();
  return out;
}

bool deleteProtocolById(int id)
{
  if (id < 1) return false;

  bool removed = false;

  // Delete the protocol text file.
  if (SPIFFS.exists(protoFileName(id))) {
    removed = SPIFFS.remove(protoFileName(id));
  }

  // Remove the "id:name" line from the management list.
  File lf = SPIFFS.open("/PROTOLIST.TXT", FILE_READ);
  String content = "";
  if (lf) { content = lf.readString(); lf.close(); }

  String out = "";
  while (content.length() > 0) {
    int nl = content.indexOf('\n');
    String l;
    if (nl >= 0) { l = content.substring(0, nl); content = content.substring(nl + 1); }
    else { l = content; content = ""; }
    // strip trailing \r
    if (l.length() > 0 && l[l.length() - 1] == '\r') l.remove(l.length() - 1);
    int colon = l.indexOf(':');
    if (colon > 0 && l.substring(0, colon).toInt() == id) {
      removed = true;
      continue; // drop this entry
    }
    out += l + "\n";
  }

  File lf2 = SPIFFS.open("/PROTOLIST.TXT", FILE_WRITE);
  if (lf2) { lf2.print(out); lf2.close(); }

  Serial.printf("deleteProtocolById: id=%d %s\n", id, removed ? "deleted" : "not found");
  return removed;
}

bool loadProtocolById(int id, String &outText)
{
  File f = SPIFFS.open(protoFileName(id), FILE_READ);
  if (!f) {
    Serial.printf("loadProtocolById: id=%d not found\n", id);
    return false;
  }
  outText = f.readString();
  f.close();
  parseConfig(outText);
  Serial.printf("loadProtocolById: loaded id=%d (%d bytes)\n", id, outText.length());
  return true;
}
