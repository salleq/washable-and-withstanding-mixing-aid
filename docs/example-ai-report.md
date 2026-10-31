# WWMA listening data

This file describes how your song behaves from start to end, in numbers. An AI assistant can't hear audio, but it can read these numbers and the spectrogram picture and help you find where in the song a problem is and what to try. Nothing was sent anywhere: this is a plain text file on your computer.

**How to use:** start a chat with an AI assistant, attach this file and the picture `WWMA report AI.png`, and paste the question below. Then ask more: "what about the bass in the second drop?"

## Question to paste

> I'm working on a House track (62 BPM), mastering it. I want it to sound good on a phone and in a car, not only in my studio. The attached file has measurements from WWMA, an analyser plugin at the end of my chain: loudness, tonal balance in six ranges, stereo width and peaks, for the whole song and for each part of it, plus a spectrogram picture. Please: 1) name the three most important things to fix and in which part of the song they happen, 2) for each, tell me what to listen for and what to try in the DAW, step by step, 3) tell me what is already fine and should be left alone. Don't trust any single number, explain when you are unsure, and remember my ears decide.

## The song

- Preset (style target): House
- Stage: mastering
- Music heard: 1:03 (silence left out)
- WWMA score: 71 / 100 (Close enough)

| Measure | Value | Off the style target by |
|---|---|---|
| Loudness | −12.8 LUFS | 3.8 |
| True peak | −0.5 dBTP | 0 |
| Punch (PLR) | 12.2 LU | 2.2 |
| Loudness range | 11.9 LU | 4.9 |
| Kick above the bass | 5.8 dB | 0 |
| Drum snap | 10.2 dB | 0 |
| Bass on phones | +0.8 dB | 0 |
| Low end width | −52.2 dB | 0 |
| Stereo width | −11.5 dB | 0 |
| Phase (correlation) | 0.87 | 0 |
| Loss in mono | −1.8 dB | 0 |
| Left/right balance | 0.0 dB | 0 |
| Tone: Sub | −2.0 dB | 0 |
| Tone: Bass | −0.3 dB | 0 |
| Tone: Mud | +0.4 dB | 0 |
| Tone: Mids | +0.3 dB | 0 |
| Tone: Harsh | −1.7 dB | 0 |
| Tone: Air | +1.1 dB | 0 |

WWMA's own findings, most important first:

1. Big level jumps between sections (LRA 11.9 LU (style max 7.0))
2. Quieter than usual for this style (Integrated -12.8 LUFS (style -9.0…-7.0))
3. Very dynamic for this style (PLR 12.2 LU (style 7.0…10.0))

## Parts of the song

WWMA split the song where the loudness, low end, top end or width change. The "looks like" names are guesses from the levels. Range columns: how much more (+) or less (-) of that range the part has than the song on average, in dB.

| Part | Starts | Length | Looks like | LUFS | Peak dBTP | Width dB | Corr. | Sub | Bass | Mud | Mids | Harsh | Air |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A | 0:00 | 0:15 | Intro | −21.8 | −11.3 | −2 | 0.23 | −40.4 | −36.0 | +5.3 | +7.6 | +5.6 | −19.1 |
| B | 0:15 | 0:17 | Drop / full part | −10.3 | −0.6 | −5 | 0.89 | +0.2 | +0.2 | −0.4 | −0.9 | −0.5 | +0.1 |
| C | 0:32 | 0:15 | Breakdown | −21.8 | −11.3 | −2 | 0.23 | −40.4 | −36.0 | +5.3 | +7.6 | +5.6 | −19.1 |
| D | 0:47 | 0:16 | Drop / full part | −10.2 | −0.5 | −5 | 0.89 | +0.2 | +0.2 | −0.4 | −0.9 | −0.5 | +0.2 |

## What changes between the parts

Nothing stood out between the parts.

## Timeline

One row per 2 s of music. Range columns are dB vs the song's average balance, as above.

| Heard | Part | LUFS | Peak | Width | Sub | Bass | Mud | Mids | Harsh | Air |
|---|---|---|---|---|---|---|---|---|---|---|
| 0:00 | A | −21.8 | −11.3 | −2 | −43.2 | −38.8 | +5.3 | +7.7 | +6.1 | −19.0 |
| 0:02 | A | −21.8 | −12.3 | −2 | −38.1 | −33.7 | +5.3 | +7.7 | +5.8 | −19.1 |
| 0:04 | A | −21.8 | −11.4 | −2 | −41.5 | −37.3 | +5.5 | +7.5 | +4.9 | −19.3 |
| 0:06 | A | −21.9 | −11.3 | −2 | −41.5 | −37.3 | +5.3 | +7.7 | +5.7 | −19.1 |
| 0:08 | A | −21.6 | −11.6 | −3 | −39.8 | −35.7 | +5.2 | +7.8 | +6.0 | −18.2 |
| 0:10 | A | −21.7 | −11.9 | −2 | −42.6 | −37.4 | +5.3 | +7.6 | +5.7 | −18.6 |
| 0:12 | A | −21.8 | −11.4 | −2 | −38.7 | −34.2 | +5.5 | +7.4 | +5.0 | −18.6 |
| 0:14 | A | −15.8 | −0.9 | −3 | −2.5 | +1.0 | +0.6 | +2.5 | +0.3 | −7.8 |
| 0:16 | B | −10.1 | −0.8 | −5 | −0.2 | +0.5 | −0.3 | −1.0 | −0.5 | −0.4 |
| 0:18 | B | −10.0 | −0.7 | −5 | −0.1 | +0.5 | −0.5 | −0.9 | −0.7 | −0.3 |
| 0:20 | B | −10.0 | −0.8 | −4 | +0.7 | −0.3 | −0.8 | −1.1 | −1.2 | −0.3 |
| 0:22 | B | −9.9 | −0.8 | −5 | +0.4 | −0.2 | −0.2 | −1.1 | −0.7 | −0.2 |
| 0:24 | B | −10.0 | −0.9 | −5 | −0.3 | +0.6 | −0.4 | −0.9 | −0.4 | −0.1 |
| 0:26 | B | −10.0 | −0.6 | −5 | −0.2 | +0.6 | −0.5 | −1.2 | −0.4 | +0.3 |
| 0:28 | B | −10.3 | −0.8 | −4 | +0.5 | −0.1 | −0.8 | −1.1 | −0.7 | +0.5 |
| 0:30 | B | −10.9 | −0.7 | −5 | +0.6 | −1.0 | +0.2 | 0.0 | +1.1 | +1.6 |
| 0:32 | C | −21.8 | −11.3 | −2 | −43.2 | −38.9 | +5.3 | +7.7 | +6.0 | −19.1 |
| 0:34 | C | −21.8 | −12.3 | −2 | −38.1 | −33.7 | +5.3 | +7.7 | +5.8 | −19.1 |
| 0:36 | C | −21.8 | −11.4 | −2 | −41.5 | −37.3 | +5.5 | +7.5 | +4.9 | −19.3 |
| 0:38 | C | −21.9 | −11.3 | −2 | −41.5 | −37.3 | +5.3 | +7.7 | +5.7 | −19.1 |
| 0:40 | C | −21.6 | −11.6 | −3 | −39.8 | −35.7 | +5.2 | +7.8 | +6.0 | −18.2 |
| 0:42 | C | −21.7 | −11.9 | −2 | −42.6 | −37.4 | +5.3 | +7.6 | +5.7 | −18.6 |
| 0:44 | C | −21.8 | −11.4 | −2 | −38.7 | −34.2 | +5.5 | +7.4 | +5.0 | −18.6 |
| 0:46 | C | −15.8 | −0.9 | −3 | −2.5 | +1.0 | +0.6 | +2.5 | +0.3 | −7.3 |
| 0:48 | D | −10.1 | −0.5 | −5 | −0.2 | +0.5 | −0.3 | −1.0 | −0.6 | −0.4 |
| 0:50 | D | −10.0 | −0.7 | −5 | −0.2 | +0.5 | −0.6 | −0.9 | −0.8 | +0.1 |
| 0:52 | D | −10.0 | −0.8 | −4 | +0.7 | −0.3 | −0.8 | −1.1 | −1.3 | −0.4 |
| 0:54 | D | −9.9 | −0.7 | −5 | +0.4 | −0.2 | −0.2 | −1.1 | −0.7 | +0.1 |
| 0:56 | D | −10.0 | −0.7 | −5 | −0.3 | +0.6 | −0.4 | −0.9 | −0.4 | 0.0 |
| 0:58 | D | −10.1 | −0.7 | −5 | −0.2 | +0.6 | −0.5 | −1.1 | −0.3 | +0.5 |
| 1:00 | D | −10.4 | −0.6 | −4 | +0.5 | −0.1 | −0.7 | −1.1 | −0.8 | +0.2 |
| 1:02 | D | −10.3 | −0.9 | −6 | +1.0 | −1.9 | +0.5 | +0.2 | +1.8 | +3.1 |

## How to read the numbers

- LUFS: loudness (EBU R128). +1 LU is a small but clear step; streaming services turn every song to about -14 LUFS.
- Peak: true peak, dBTP. Above about -1 it can distort when turned into MP3/AAC.
- Width: side vs mid level above 140 Hz, dB. -10 is wide, -25 narrow. Correlation near 1 = close to mono, below 0 = parts cancel in mono.
- Ranges: Sub 18-56 Hz, Bass 56-140 Hz, Mud 140-445 Hz, Mids 445-2.2k Hz, Harsh 2.2k-5.7k Hz, Air 5.7k-18.0k Hz.
- The spectrogram: time left to right, low notes at the bottom, brighter = louder. Lines below it: loudness and width. Letters mark the parts.

---

Your ears decide. Neither WWMA nor an AI can hear your song: check every suggestion by listening, on a phone and in a car too.
