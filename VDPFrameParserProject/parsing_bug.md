# Bug Report: Frame Parsing Failures on Android

**Reported by:** Mobile Team  
**Date:** 2025-01-20  
**Severity:** High  
**Affected Platforms:** Android only (iOS works fine)

## Description
Users on Android devices are experiencing intermittent failures when reading diagnostic data. The app shows "Communication Error" approximately 30% of the time when sending consecutive frames.

## Steps to Reproduce
1. Connect to vehicle
2. Send READ_DATA command (0x10) to ECU 0x01
3. Immediately send another READ_DATA command to ECU 0x02
4. Repeat rapidly 10 times

## Expected Behavior
All frames should be parsed correctly and responses received within 100ms.

## Actual Behavior
- On iOS: Works correctly 100% of the time
- On Android: Fails ~30% of the time with parsing errors

## Debug Logs from Android
```
[DEBUG] Raw buffer: 7E0A8110001234569F7F7E0A821000ABCDEF
[ERROR] Failed to parse frame: Invalid frame structure
[DEBUG] Buffer position: 20
[DEBUG] Remaining bytes: 16
[DEBUG] Note: Second frame appears truncated
```

## Additional Information
- The issue seems worse on slower Android devices
- Sometimes the parser seems to "lose sync" and treats response data as a new frame start
- When it fails, subsequent frames also fail until the app is restarted
- Customer reports: "It works if I wait a second between commands"
- Network monitor shows all bytes are received correctly
- iOS typically processes frames immediately, Android may batch them

## Current Parser Pseudocode (from mobile team)
```
while (buffer.hasData()) {
    if (buffer[0] != 0x7E) {
        buffer.consumeByte();
        continue;
    }
    
    frame = parseFrame(buffer);
    if (frame.isValid()) {
        processFrame(frame);
    }
}
```

## Mobile Team's Question
"Could this be related to how the C++ parser handles partial frames in the buffer? We're calling the parser from a background thread on Android, while iOS calls it from the main thread. Also, we're not sure if we should be calling processIncomingData or sendRawData for incoming bytes - the documentation isn't clear."

## Root Cause Analysis
- Check the logs. From the logs it is evident that the frames are batched, and the 2nd frame is not complete!
- From the code, the parser logic `parseFrame(buffer)` assumes a valid frame is returned if the sync byte is found, but from the logs we can see that the partial 2nd frame is causing parsing issues. This explains that the parsing logic is not robust in handling the scenario. 
- The the device behaviour, it is evident that it works only on IOS, most probaly since it is single threaded, and the partial incomplete frame scenario does not occur! While on Android,  background thread is likely triggered as soon as any data arrives, even if it's just a partial frame. Also it is said that the delay helps, which confirms the analysis, that if given enough time, the whole frames are received, no premature parsing happens and the issue does not occur.

## Solution
This is solved in the VDP Parser. In brief, if parseFrame() fails, consume only one byte and check for the next start marker. Buffer the bytes based on length. Avoid skipping or clearing the buffer unless you're sure it's invalid.
Continuously feed() the parser all incoming data from the network.
Call extractFrames() after every feed to pull out any frames that have been completed.
Also add detailed documentation on the exposed public APIs, so that it is easier for the consumer of the APIs.