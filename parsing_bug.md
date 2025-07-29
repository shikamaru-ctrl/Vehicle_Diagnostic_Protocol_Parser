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