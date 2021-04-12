CWSL_DIGI is a program for the CWSL architecture for decoding WSJT-X digital modes and sending reports to spotting networks such as PSK Reporter, Reverse Beacon Network, and WSPRNet. CWSL_DIGI was developed by W2AXR, with testing and feedback from WZ7I and W3OA.

CWSL_DIGI is a Windows console application. For command-line usage, run CWSL_DIGI.exe --help at a Windows Command Prompt.

A sample configuration file named "config.ini" should be provided with the program. For most use cases, a configuration file is sufficient to provide all input:
CWSL_DIGI.exe --configfile C:\CWSL_DIGI\config.ini

Before running CWSL_DIGI be sure to edit the configuration file to specify the proper callsign, grid square, and decoder configuration.

Problem reports and feature requests can be sent to W2AXR, alexranaldi@gmail.com, or filed as issues on Github.

The latest executable compiled by W2AXR can be found here: https://drive.google.com/drive/folders/1hyegKu3OcIvSVkvFWd29VfqL6l8OYprf
It is compiled with Visual Studio 2019 Pro, for Windows 64-bit.

CWSL_DIGI is released under the GNU GENERAL PUBLIC LICENSE, Version 3.

See the file SOURCE_CODE.txt for a link to the source code on Github.
