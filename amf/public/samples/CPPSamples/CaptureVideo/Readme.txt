### Build the sample application
* Download BlackMagic 'Desktop Video 10.9.10 SDK' and copy the files under the folder 'Blackmagic DeckLink SDK 10.9.10\Win\include' to 'Engine\Source\ThirdParty\AMD\AMF_SDK\Thirdparty\BlackmagicDeckLinkSDK\10.9.10\Win\include'
* Load Visual Studio&reg; 2017 solution 'Engine\Source\ThirdParty\AMD\AMF_SDK\amf\protected\samples\CPPSamples_vs2017.sln'.
* build the 'CaptureVideo' project

### Run the sample application 'CaptureVideo'
* Launch 'CaptureVideo'.
* Select foreground source, video clip or camera live feed 
   * Select video clip, menu 'File'->'Media File'
   * Select camera feed, menu 'File'->'Capture Device'->'DeckLink 4k Extreme 12G'
* Select background clip, menu 'File'->'Background Media File'
*Run, menu 'Run'

### Enabling DirectGMA
* Open the AMD FirePro Control Center and go to the SDI/DirectGMA tab to enable DirectGMA 

### Options
##color pick
Key color picker:	LeftClick
second Key color picker: ctrl+LeftClick
reset : Alt+LeftClick

##ON/OFF switch
spill suppression:	'q'
Color adjust     :	'w'  //off, on
Advanced adjust  :	'e'  //off, on, transparent handling
bokeh            :	'r'  //off, background, foreground
Bypass           :	't'  //off, foreground, background
Edge	         :	'y'  //debug only for now

##Range adjust
Threshold Min:   'a', 's'; default=8
Threshold Max:   'd', 'f'; default=10
boken radiu:     'g', 'h'; default=5
Luma threshold:  'j', 'k'; default=40

spill threshold: 'z', 'x'; default=5
Color adjust:    'c', 'v'; default=7
Color adjust2:   'b', 'n'; default=52

##Scaling & Positioning of foreground image
Ctrl+Shift + '+'/'-' ; 10% step
Ctrl + '+'/'-' ;       1% step

Ctrl+Shift + 'arrow key' ; 10 pixel step
Ctrl + 'arrow key'       ; 1 pixel step

