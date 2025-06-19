# NTRIP-Analyser.

This repository contains my work on developing an NTRIP RTCM 3.x stream analyser.

## About this project.

The primary goal of this project is to deepen my understanding of NTRIP streams, a field where open-source tools and public information are limited. By building this analyser, I aim to explore and learn the structure and content of RTCM 3.x messages transmitted over NTRIP.

A secondary goal is to practice and experiment with programming, leveraging AI tools such as GitHub Copilot. Please note that, while AI assistance has accelerated development, I cannot guarantee the originality or accuracy of all code segments, as the sources used by large language models are not always transparent or verifiable. The results and information presented here have not been exhaustively validated. As such, I advise caution: **do not rely on this code or its output for critical applications without independent verification.** The included disclaimer applies in full.

## Functionalities

The code will allow to do the following analysis on NTRIP streams:

1. Retrieve mountpointlist from a caster and display it on screen.
2. Login to a caster and start a NTRIP stream. At reception:  
   a. Decode all implemented NTRIP messages  
   b. Message overview: Analyse during a time (default 60 seconds) all received NTRIP messages and:  
      - Count and present the message numbers received  
      - Present minimum, average and maximum interval time between two of the same message number.  
   c. Filtered message decoding  
   d. Satellite analysis: Analyse during a time (default 60 seconds) all received NTRIP messages and:  
      - Count the unique satellites reported  
      - Present the reported unique satellites per GNSS system  
      - Present the total number of satellites seen.

## Documentation

[Documentation](docs/readme.md) is in the documentation folder.

## License and disclaimer. 

Please note the license at the end of this document. 

# License
This project is free: You can redistribute it and/or modify it under the terms of a Creative Commons Attribution-NonCommercial 4.0 International License (http://creativecommons.org/licenses/by-nc/4.0/) by Remko Welling (https://ese.han.nl/~rwelling) E-mail: remko.welling@han.nl

<a rel="license" href="http://creativecommons.org/licenses/by-nc/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-nc/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-nc/4.0/">Creative Commons Attribution-NonCommercial 4.0 International License</a>.

# Disclaimer
This project is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.