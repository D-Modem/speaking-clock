# How it keeps time

The old machines played one recording, and all callers listened to it.
The time mark (the third pip, or the tone) came exactly on the second
that the recording announced. Callers started to listen in the middle of
a sentence.

`SpeakingClock()` operates in the same way. The module aligns each
announcement to the system clock, sample by sample. Thus, keep the
system clock on NTP. The module checks the clock each second. If the
clock drifts by more than 20 ms, the module moves back to the clock. The
module does not put announcements in a queue with `Playback()`. Thus the
clock does not drift, for calls of all lengths. On a test call, the third
pip of Pat Simmons came less than 10 ms from the second.
mod_speakingclock does the same on FreeSWITCH.
