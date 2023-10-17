# Overview #

CWSL_DIGI uses the [CWSL](https://github.com/HrochL/CWSL) utility to decode WSJT-X digital modes including FT8, FT4, JT65, WSPR, FST4, FST4W, and JS8 and sends spots to [PSK Reporter](https://www.pskreporter.info/pskmap.html), [Reverse Beacon Network](https://www.reversebeacon.net/), and [WSPRNet](https://www.wsprnet.org/drupal/wsprnet/map). CWSL_DIGI was developed by W2AXR, with testing and feedback from WZ7I, W3OA, 9V1RM, N4ZR, K9IMM and MM3NDH.

With CWSL_DIGI, [CW Skimmer Server](http://www.dxatlas.com/SkimServer/), and an appropriate SDR (such as a [Red Pitaya](https://redpitaya.com/red-pitaya-for-radio-amateurs-sdr/) or [QS1R](https://www.ab9il.net/software-defined-radio/sdr2.html)), is possible to decode CW and multiple digital modes across multiple bands, all simultaneously.

Problem reports and feature requests can be sent to W2AXR, alexranaldi@gmail.com, or filed as issues on Github.

CWSL_DIGI is released under the GNU GENERAL PUBLIC LICENSE, Version 3.


# Latest Release #

The latest version is 0.88 - [Download Windows 64-bit zip](https://github.com/alexranaldi/CWSL_DIGI/archive/refs/tags/0.88-Release.zip)

Notable changes in 0.88 include:
Enhancement: FT8 - support Fox/Hound messages. This will increase the number of FT8 spots significantly if you skim the DX frequencies when an expedition is going on!
Enhancement: JS8 - support for some JS8 message formats. Requires JS8Call to be installed.
Enhancement: Supports SOTAmat FT8 messages (Resolves https://github.com/alexranaldi/CWSL_DIGI/issues/6)
Fix: use hostname instead of IP Address when connecting to WSPRNet and PSK Reporter. This resolves the ongoing issue with 0.86 and earlier being unable to contact WSPRNet.
Fix: FST4 and FST4W decoding bugs that prevented successful decoding in many cases.
Fix: several bugs in callsign handling and bad callsign detection.
Fix: Improve PSK Reporter connection reliability.
## Installation ##

1. Install and configure [CW Skimmer Server](http://www.dxatlas.com/SkimServer/). The detailed steps can be found on the CW Skimmer Server website.
2. Install [CWSL](https://github.com/HrochL/CWSL). Configure it with CW Skimmer Server. The detailed steps can be found on the CWSL page.
3. Install the latest [Microsoft Visual Studio 2022 redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe). This may not be required if your computer is up to date.
4. Install the [Intel Fortran redistributable](https://drive.google.com/drive/folders/1pk99ruANXTd_87oLce2L32H2V-P6dAFn?usp=drive_link). This may not be required if your computer is up to date.
5. Download the latest release of CWSL_DIGI. See download link above. Unzip into a folder of your choice. e.g., C:\CWSL_DIGI
6. Configure CWSL_DIGI by editing the config.ini file. This step is required. See the detailed Configuration section below.
7. Start CWSL_DIGI by running CWSL_DIGI.exe.

## Configuration ##

CWSL_DIGI is configured by editing the config.ini file that comes with CWSL_DIGI. 

1. Open config.ini in your favorite text editor, such as notepad or Notepad++.
2. Any line beginning with a # character is a comment and will be ignored by CWSL_DIGI. The file contains comments to guide the user in editing. 
3. Set the operator callsign and grid square.
    1. Replace *MYCALL* with your callsign.
    2. Replace *AB01xy* with your 6 character grid square.
4. Enable at least one decoder. It is suggested to start with a single decoder, and once working, enable additional decoders.
    1. Find the [decoders] section in the file.
    2. To enable a decoder, remove the # character at the beginning of the line. For example, to enable 10m FT8:
    
```
# 10m
#decoder=28180000 FT4
decoder=28074000 FT8
#decoder=28076000 JT65
```

NOTE: The bandwidth selected in CW Skimmer Server and the center frequencies for each band may limit which frequencies CWSL_DIGI can access. CWSL_DIGI has access to only the bands and frequencies setup in CW Skimmer Server, which must be kept in mind when enabling decoders.

5. Enable reporting to spotting networks, if desired.
    1. Find the [reporting] section.
    2. Set *pskreporter*, *rbn* and *wsprnet* to true to enable spotting, as desired. Note that Reverse Beacon Network additionally requires Aggregator - additional steps are detailed below.

## Running CWSL_DIGI ##

CWSL_DIGI is best run from a Command prompt. While not required, this allows console output, including error messages, to be viewed if the program crashes or terminates unexpectedly.

1. Open a Command prompt.
2. Change to the directory where CWSL_DIGI was installed (Extracted from zip)
3. Run CWSL_DIGI.exe


### Reverse Beacon Network ###

For spotting to [Reverse Beacon Network (RBN)](https://www.reversebeacon.net/index.php), additional software called [Aggregator](https://www.reversebeacon.net/pages/Aggregator+34) is required.

1. In the CWSL_DIGI config.ini file, set rbn=true
2. Install [Aggregator](https://www.reversebeacon.net/pages/Aggregator+34).
3. In Aggregator, select the *FT#* tab.
    1. Put a check next to source number 40
    2. Make sure it is set to port 2215
    3. When running properly, the white box at the upper right will begin to display FT4/8 spots. Only CQ spots are submitted to RBN, and duplicates are culled.
5. Confirm that FT4/FT8 spots are present on the Status page of Aggregator.


### Calibration ###

1. If desired, read the file [CALIBRATION.txt](./CALIBRATION.txt) for instructions on how to configure receiver calibration in CWSL_DIGI.


## Troubleshooting ##

If the program opens and then closes immediately, run CWSL_DIGI from a Command Window. See the "Running" section above.

