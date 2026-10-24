import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: root
    width: 1024; height: 600; minimumWidth: 800; minimumHeight: 480
    visible: true
    visibility: Window.FullScreen
    title: "STORM MIX Native 0.6.39"
    color: "#07121d"
    property int page: 0
    // The physical BCF2000 bank is the single source of truth.
    // A bank change also brings the relevant touch page to the front.
    readonly property var bcfBanks: ["CH 1–7", "CH8 AUX MAIN", "SEND AUX1", "SEND AUX2",
        "SEND AUX3", "SEND AUX4", "CH EQ GAIN", "CH EQ FREQ", "CH EQ Q",
        "CH GATE", "CH COMP", "CH LIMIT", "MAIN EQ", "AUX EQ",
        "BUS DYN", "FX", "OUTPUT DSP", "ROUTING"]
    readonly property string bcfBankName: bcfBanks[mixer.midiBank] || "BCF"
    Connections {
        target: mixer
        function onMidiBankChanged() {
            var b=mixer.midiBank
            if(b===0)root.page=0
            else if(b===1)root.page=1
            else if(b>=2&&b<=5)root.page=4
            else if(b>=6&&b<=8)root.page=2
            else if(b>=9&&b<=11)root.page=3
            else if(b===12)root.page=10
            else if(b===13||b===14)root.page=9
            else if(b===15)root.page=6
            else if(b===16)root.page=5
            else root.page=4
        }
    }

    property int eqBank: 0
    property bool analyzerRta: false
    // Vertical analyzer range: 30–120 dB, anchored at 0 dBFS.
    property int analyzerRange: 60
    // EQ transfer curve independently supports +/- 12 to +/- 72 dB.
    property int eqVerticalRange: 24
    // Depend explicitly on the revision signal so QML re-evaluates C++ invokables.
    function channelDynOn(ch,unit) {var revision=mixer.parameterRevision;return mixer.dynamicsEnabled(ch,unit)}
    function busDynOn(bus,unit) {var revision=mixer.parameterRevision;return mixer.busDynamicsEnabled(bus,unit)}
    function outputDynOn(out,unit) {var revision=mixer.parameterRevision;return mixer.outputDynamicsEnabled(out,unit)}
    property int selectedAux: 0
    property int selectedBand: 0
    property int selectedOutputBand: 0
    property int selectedOutputDyn: 1
    property int selectedOutputView: 0
    onPageChanged: {
        if(page===2) mixer.setSpectrumSource(mixer.selected)
        else if(page===9) mixer.setSpectrumSource(8+selectedAux)
        else if(page===10) mixer.setSpectrumSource(12)
        else if(page===5) mixer.setSpectrumSource(13+Math.min(1,mixer.selectedOutput))
    }
    onSelectedAuxChanged: if(page===9)mixer.setSpectrumSource(8+selectedAux)
    Connections {
        target: mixer
        function onSelectedChanged() {
            if(root.page===2) mixer.setSpectrumSource(mixer.selected)
        }
    }

    property int dynUnit: 1
    property int busDynUnit: 1
    property var tabs: ["MIXER", "AUX", "CHANNEL", "DYNAMICS", "ROUTING", "OUTPUT", "FX", "METERS", "SYSTEM", "AUX DSP", "MAIN DSP", "SHOW"]
    readonly property color cyan: "#42d9f5"
    readonly property color panel: "#15263a"
    readonly property color borderCol: "#31536c"
    function db(v) {return Number(v) <= -60 ? "−∞" : Number(v).toFixed(1)+" dB"}

    component Btn: Button {
        id: b
        property bool active: false
        property color activeColor: "#087e9d"
        implicitHeight: 31
        font.pixelSize: 11; font.bold: true
        contentItem: Text { text: b.text; font: b.font; color: "#f1f8ff"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { radius: 5; color: b.active ? b.activeColor : b.down ? "#365775" : "#1b3048"; border.color: b.active ? "#55dff7" : "#375773" }
    }
    component VUMeter: Item {
        id: meter
        property real level: -90
        implicitWidth: 17; implicitHeight: 230
        Rectangle { anchors.fill: parent; radius: 3; color: "#06141f"; border.color: "#42627b" }
        Rectangle {
            x:3; width: parent.width-6; anchors.bottom: parent.bottom; anchors.bottomMargin: 3
            height: Math.max(0, (parent.height-6)*Math.min(1,Math.max(0,(meter.level+60)/60)))
            radius:2
            gradient: Gradient {
                GradientStop {position:0; color:"#ed5264"}
                GradientStop {position:0.2; color:"#ffc84a"}
                GradientStop {position:0.45; color:"#64dc7c"}
                GradientStop {position:1; color:"#16a875"}
            }
        }
        Repeater { model:16; Rectangle { x:2; y:(index+1)*meter.height/17; width:meter.width-4; height:2; color:"#071724" } }
    }
    component GRBar: Item {
        id: grBar
        property real reduction: 0
        property color barColor: "#f8b85d"
        implicitWidth: 8; implicitHeight: 230
        Rectangle {anchors.fill:parent;radius:3;color:"#091824";border.color:"#42627b"}
        Rectangle {
            x:2;width:parent.width-4;y:2;radius:2
            height:Math.max(0,(parent.height-4)*Math.min(1,Math.max(0,grBar.reduction)/24))
            color:grBar.reduction>9?"#f06a65":grBar.barColor
        }
    }
    // EQ spectrum: shared logarithmic frequency coordinate system with EqGraph.
    component SpectrumOverlay: Item {
        id: analyzer
        property var bins: mixer.spectrum
        property bool enabledSpectrum: true
        // Fixed-rate repaint: avoid a restart/debounce timer that can
        // indefinitely postpone paints during continuous FFT updates.
        property bool spectrumDirty: true
        onBinsChanged: spectrumDirty=true
        Timer {
            id:spectrumRepaint
            interval:33
            repeat:true
            running:analyzer.visible && analyzer.enabledSpectrum
            onTriggered: {
                if(analyzer.spectrumDirty){
                    analyzer.spectrumDirty=false
                    spectrumCanvas.requestPaint()
                }
            }
        }
        Connections {
            target:root
            function onAnalyzerRtaChanged(){spectrumCanvas.requestPaint()}
            function onAnalyzerRangeChanged(){spectrumCanvas.requestPaint()}
        }
        Canvas {
            id:spectrumCanvas
            anchors.fill:parent
            antialiasing:true
            renderTarget:Canvas.Image
            onPaint: {
                var c=getContext("2d"),w=width,h=height
                c.clearRect(0,0,w,h)
                var bins=mixer.spectrum, peaks=mixer.spectrumPeaks, rta=mixer.rtaBands, rtaHeld=mixer.rtaPeaks
                if(!analyzer.enabledSpectrum || !bins || bins.length<192 || w<=0 || h<=0)return
                // Same log x-axis as EQ response. Catmull-Rom is presentation smoothing,
                // not an increase in the underlying FFT frequency resolution.
                // FFT samples represent logarithmic frequency CENTERS:
                // f[k] = 20 * 1000^((k+0.5)/192). Align them with the EQ axis.
                function hzAtX(x) {return 20*Math.pow(1000,Math.max(0,Math.min(1,x/Math.max(1,w))))}
                function xAtHz(f) {return w*Math.log(Math.max(20,Math.min(20000,f))/20)/Math.log(1000)}
                function sample(x) {
                    var t=Math.max(0,Math.min(191,192*Math.log(hzAtX(x)/20)/Math.log(1000)-0.5)),i=Math.floor(t),u=t-i
                    var a=Number(bins[Math.max(0,i-1)]),b=Number(bins[i])
                    var d=Number(bins[Math.min(191,i+1)]),e=Number(bins[Math.min(191,i+2)])
                    return Math.max(-90,Math.min(0,.5*((2*b)+(-a+d)*u+(2*a-5*b+4*d-e)*u*u+(-a+3*b-3*d+e)*u*u*u)))
                }
                var baseline=h-20
                if(root.analyzerRta) {
                    // 60 independent 1/6-octave bands measured in C++.
                    var count=60
                    for(var band=0;band<count;band++){
                        // Every bar uses its actual sixth-octave frequency edges,
                        // projected onto the same log X axis as the EQ curve.
                        var center=20*Math.pow(2,band/6)
                        var low=center/Math.pow(2,1/12)
                        var high=center*Math.pow(2,1/12)
                        var x0=xAtHz(low),x1=xAtHz(high)
                        if(x1<=x0)continue
                        var gap=Math.max(.5,(x1-x0)*.10)
                        var bx=x0+gap/2,bw=Math.max(1,x1-x0-gap)
                        // Band powers computed from raw FFT bins in C++.
                        var peak=Number(rta[band])
                        var fraction=Math.max(0,Math.min(1,(peak+root.analyzerRange)/root.analyzerRange))
                        var barHeight=(baseline-8)*fraction
                        c.fillStyle=peak>-12?"rgba(244,105,95,.92)":peak>-30?"rgba(246,191,90,.9)":"rgba(92,217,218,.85)"
                        c.fillRect(bx,baseline-barHeight,bw,barHeight)
                        // Bright short line = measured peak held for ~350ms.
                        var held=Number(rtaHeld[band])
                        var peakFraction=Math.max(0,Math.min(1,(held+root.analyzerRange)/root.analyzerRange))
                        var py=baseline-(baseline-8)*peakFraction
                        c.fillStyle="rgba(248,250,253,.96)"
                        c.fillRect(bx,py,bw,2)
                    }
                    return
                }
                c.beginPath()
                for(var x=0;x<=w;x+=1.5){
                    var db=sample(x),y=8+(baseline-8)*(1-Math.max(0,Math.min(1,(db+root.analyzerRange)/root.analyzerRange)))
                    if(x===0)c.moveTo(x,y);else c.lineTo(x,y)
                }
                c.lineTo(w,baseline);c.lineTo(0,baseline);c.closePath()
                c.fillStyle="rgba(158,181,196,0.19)";c.fill()
                c.beginPath()
                for(var x=0;x<=w;x+=1.5){
                    var db=sample(x),y=8+(baseline-8)*(1-Math.max(0,Math.min(1,(db+root.analyzerRange)/root.analyzerRange)))
                    if(x===0)c.moveTo(x,y);else c.lineTo(x,y)
                }
                c.strokeStyle="rgba(198,223,238,0.98)";c.lineWidth=1.8;c.lineJoin="round";c.stroke()
                // Secondary fine peak-hold trace: shows transient maxima.
                c.beginPath()
                for(var px=0;px<=w;px+=2){
                    var idx=Math.max(0,Math.min(191,Math.round(px/w*191)))
                    var heldDb=Number(peaks[idx])
                    var heldY=8+(baseline-8)*(1-Math.max(0,Math.min(1,(heldDb+root.analyzerRange)/root.analyzerRange)))
                    if(px===0)c.moveTo(px,heldY);else c.lineTo(px,heldY)
                }
                c.strokeStyle="rgba(252,198,91,.9)";c.lineWidth=1;c.stroke()
            }
        }
    }
    // Touch + mouse: use explicit pointer handling. Never assign to 'value' here:
    // its binding to the C++ channel must remain intact across model updates.
    component VFader: Item {
        id: f
        property real value: 0
        property real liveValue: value
        property bool dragging: false
        Timer {
            id: faderCommit
            interval: 55
            repeat: false
            onTriggered: f.userChanged(f.liveValue)
        }
        onValueChanged: {if(!dragging) liveValue=value}
        property real from: -60
        property real to: 10
        signal userChanged(real newValue)
        implicitWidth: 45; implicitHeight: 225
        readonly property real travel: Math.max(1,height-30)
        // Audio-console fader taper: 0 dB near 75% of travel, finer
        // resolution around unity than a linear dB mapping.
        function dbFromPosition(p) {
            p=Math.max(0,Math.min(1,p))
            var positions=[0,0.12,0.30,0.48,0.62,0.75,0.88,1]
            var db=[-60,-45,-30,-18,-8,0,5,10]
            for(var i=1;i<positions.length;i++)if(p<=positions[i])
                return db[i-1]+(db[i]-db[i-1])*(p-positions[i-1])/(positions[i]-positions[i-1])
            return 10
        }
        function positionFromDb(db) {
            db=Math.max(-60,Math.min(10,db))
            var positions=[0,0.12,0.30,0.48,0.62,0.75,0.88,1]
            var values=[-60,-45,-30,-18,-8,0,5,10]
            for(var i=1;i<values.length;i++)if(db<=values[i])
                return positions[i-1]+(positions[i]-positions[i-1])*(db-values[i-1])/(values[i]-values[i-1])
            return 1
        }
        readonly property real normalized: positionFromDb(liveValue)
        function handleY(y) {
            var fraction = Math.max(0,Math.min(1,(height-15-y)/travel))
            var newValue = Math.round(dbFromPosition(fraction)*10)/10
            if (Math.abs(liveValue-newValue) >= 0.05) {
                liveValue=newValue
                if (!faderCommit.running) faderCommit.start()
            }
        }
        Rectangle { anchors.horizontalCenter:parent.horizontalCenter; y:7; width:10; height:parent.height-14; radius:5; color:"#061522"; border.color:"#47677c" }
        Rectangle { anchors.horizontalCenter:parent.horizontalCenter; y:12; width:2; height:parent.height-24; color:"#31556d" }
        Rectangle {
            x:(parent.width-width)/2; y: 4+(1-f.normalized)*f.travel
            width:39; height:23; radius:4; border.width:1; border.color:"#d9edf9"
            gradient: Gradient {
                GradientStop {position:0; color:"#8299ad"}
                GradientStop {position:0.28; color:"#edfaff"}
                GradientStop {position:0.55; color:"#c4d5e2"}
                GradientStop {position:1; color:"#5e7b90"}
            }
            Rectangle {anchors.centerIn:parent; width:parent.width-8; height:2; color:"#496071"}
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            acceptedButtons: Qt.LeftButton
            onPressed: function(mouse) {f.dragging=true; f.handleY(mouse.y)}
            onPositionChanged: function(mouse) {if(pressed) f.handleY(mouse.y)}
            onReleased: {faderCommit.stop(); f.userChanged(f.liveValue); f.dragging=false}
            onDoubleClicked: {faderCommit.stop(); f.liveValue=0; f.userChanged(0)}
            onCanceled: {faderCommit.stop(); f.dragging=false; f.liveValue=f.value}
        }
    }
    component Strip: Rectangle {
        id: strip
        property int idx: 0
        property var channelData: ({})
        property bool isAux: false
        signal edit()
        color: root.panel; radius:7; border.color:root.borderCol
        Rectangle {x:6; y:5; width:parent.width-12; height:4; radius:2; color:strip.isAux?"#ffbd60":(strip.channelData.color || root.cyan)}
        ColumnLayout {
            anchors.fill:parent; anchors.leftMargin:6; anchors.rightMargin:6; anchors.bottomMargin:6; anchors.topMargin:16; spacing:3
            Text {text:strip.channelData.name || "CH"; color:"#fff"; font.bold:true; font.pixelSize:12; Layout.fillWidth:true; Layout.preferredHeight:17; verticalAlignment:Text.AlignVCenter; horizontalAlignment:Text.AlignHCenter; elide:Text.ElideRight}
            Item {
                id: faderArea; Layout.fillWidth:true; Layout.fillHeight:true; Layout.minimumHeight:100
                Row {
                    anchors.centerIn:parent; spacing:5
                    VFader {
                        id:channelFader
                        height:Math.max(90,faderArea.height-6)
                        value:Number(strip.channelData.fader === undefined ? 0 : strip.channelData.fader)
                        onUserChanged: function(v) {
                            if(strip.isAux) mixer.setAux(strip.idx,"fader",v)
                            else mixer.setChannel(strip.idx,"fader",v)
                        }
                    }
                    VUMeter {height:Math.max(90,faderArea.height-6); level:Number(strip.isAux ? mixer.auxMeters[strip.idx] : mixer.channelMeters[strip.idx])}
                    GRBar {height:Math.max(90,faderArea.height-6);reduction:Number(strip.isAux?mixer.auxGR[strip.idx]:mixer.channelGR[strip.idx])}
                }
            }
            RowLayout {Layout.fillWidth:true; spacing:2
                Rectangle {Layout.fillWidth:true; Layout.preferredHeight:23; color:"#061924"; border.color:"#2d5069"; radius:4
                    Text {anchors.centerIn:parent; text:root.db(channelFader.liveValue); color:root.cyan; font.bold:true; font.pixelSize:12}
                }
                Btn {text:"↺"; Layout.preferredWidth:24; onClicked:channelFader.userChanged(0)}
            }
            Text {text:Number(strip.isAux ? mixer.auxMeters[strip.idx] : mixer.channelMeters[strip.idx]).toFixed(1)+" dBFS";color:"#8bc9e4";font.pixelSize:10;Layout.alignment:Qt.AlignHCenter}
            RowLayout {Layout.fillWidth:true; spacing:3
                Btn {text:"M"; active:!!strip.channelData.mute; activeColor:"#b02b4a"; Layout.fillWidth:true; Layout.minimumWidth:0; onClicked:strip.isAux?mixer.setAux(strip.idx,"mute",!strip.channelData.mute):mixer.setChannel(strip.idx,"mute",!strip.channelData.mute)}
                Btn {text:"S"; active:!!strip.channelData.solo; activeColor:"#9c701c"; Layout.fillWidth:true; Layout.minimumWidth:0; onClicked:strip.isAux?mixer.setAux(strip.idx,"solo",!strip.channelData.solo):mixer.setChannel(strip.idx,"solo",!strip.channelData.solo)}
                Btn {text:"PFL"; active:!!strip.channelData.pfl; Layout.fillWidth:true; Layout.minimumWidth:0; onClicked:strip.isAux?mixer.setAux(strip.idx,"pfl",!strip.channelData.pfl):mixer.setChannel(strip.idx,"pfl",!strip.channelData.pfl)}
            }
            Text {text:"GR " + Number(strip.isAux?mixer.auxGR[strip.idx]:mixer.channelGR[strip.idx]).toFixed(1)+" dB";color:"#ffbd60";font.pixelSize:10;Layout.alignment:Qt.AlignHCenter}
            Btn {text:strip.isAux?"AUX DSP":"GAIN / EQ"; Layout.fillWidth:true; onClicked:strip.edit()}
        }
    }
    component Rack: Rectangle {
        color:root.panel; radius:8; border.color:root.borderCol
    }
    // Stable parameter control: backend value stays bound, while the pointer owns the
    // displayed value during interaction. Writes are coalesced to reduce QSettings/DSP load.
    component KnobSlider: ColumnLayout {
        id: ks
        property string label: ""
        property real from: 0
        property real to: 100
        property real value: 0
        property int decimals: 1
        property real shownValue: value
        property bool dragging: slider.pressed
        signal changed(real v)
        property real resetValue: 0
        spacing: 5
        onValueChanged: { if (!dragging && !commitTimer.running) shownValue = value }
        Timer {
            id: commitTimer
            interval: 55
            repeat: false
            onTriggered: ks.changed(ks.shownValue)
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: ks.label; color: "#d5e9f6"; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Btn {
                text:"↺"
                implicitWidth:30; implicitHeight:26
                ToolTip.visible: hovered
                ToolTip.text: "Ripristina valore predefinito"
                onClicked: {
                    commitTimer.stop()
                    ks.shownValue=Math.max(ks.from,Math.min(ks.to,ks.resetValue))
                    ks.changed(ks.shownValue)
                }
            }
            Rectangle {
                implicitWidth: 75; implicitHeight: 26; radius: 5
                color: "#081c2b"; border.color: ks.dragging ? "#42d9f5" : "#31536c"
                Text { anchors.centerIn: parent; text: Number(ks.shownValue).toFixed(ks.decimals); color: "#42d9f5"; font.pixelSize: 13; font.bold: true }
            }
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.preferredHeight: 27
            from: ks.from; to: ks.to
            value: ks.shownValue
            onMoved: {
                ks.shownValue = value
                commitTimer.restart()
            }
            onPressedChanged: {
                if (!pressed) {
                    commitTimer.stop()
                    ks.changed(ks.shownValue)
                }
            }
            background: Item {
                x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - 4
                width: slider.availableWidth; height: 8
                Rectangle { anchors.fill: parent; radius: 4; color: "#071725"; border.color: "#3a607a" }
                Rectangle { width: Math.max(0, slider.visualPosition * parent.width); height: parent.height; radius: 4; color: "#168eae" }
            }
            MouseArea {
                anchors.fill: parent; acceptedButtons: Qt.LeftButton
                onDoubleClicked: {commitTimer.stop();ks.shownValue=Math.max(ks.from,Math.min(ks.to,ks.resetValue));ks.changed(ks.shownValue)}
                onPressed: function(mouse){mouse.accepted=false}
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                implicitWidth: 23; implicitHeight: 23; radius: 6
                color: slider.pressed ? "#ffffff" : "#c5e5f2"
                border.width: 2; border.color: slider.pressed ? "#42d9f5" : "#568aa6"
                Rectangle { anchors.centerIn: parent; width: 3; height: 11; radius: 1; color: "#39647d" }
            }
        }
    }

    component CrossoverGraph: Item {
        id:cross
        property int out:0
        property int revision:mixer.parameterRevision
        readonly property real leftPad:32
        readonly property real rightPad:12
        readonly property real topPad:8
        readonly property real bottomPad:23
        function hzX(f){return leftPad+(width-leftPad-rightPad)*Math.log(Math.max(20,Math.min(20000,f))/20)/Math.log(1000)}
        Rectangle {anchors.fill:parent;radius:6;color:"#071726";border.color:"#42647a"}
        Canvas {id:cv;anchors.fill:parent;renderTarget:Canvas.Image
            onPaint:{
                var c=getContext("2d"),w=width,h=height;c.clearRect(0,0,w,h)
                var left=cross.leftPad,right=w-cross.rightPad,top=cross.topPad,bottom=h-cross.bottomPad
                if(right<=left||bottom<=top)return
                var ticks=[20,50,100,200,500,1000,2000,5000,10000,20000]
                c.font="10px sans-serif";c.textBaseline="top";c.lineWidth=1
                for(var i=0;i<ticks.length;i++){
                    var x=cross.hzX(ticks[i]);c.strokeStyle="#27455b"
                    c.beginPath();c.moveTo(x,top);c.lineTo(x,bottom);c.stroke()
                    c.fillStyle="#b7c9d8";c.textAlign=i===0?"left":i===ticks.length-1?"right":"center"
                    c.fillText(ticks[i]>=1000?(ticks[i]/1000)+"k":String(ticks[i]),x,bottom+5)
                }
                for(var db=0;db>=-60;db-=20){
                    var y=top+(-db/60)*(bottom-top)
                    c.strokeStyle="#233e50";c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke()
                    c.fillStyle="#93b0c4";c.textAlign="right";c.textBaseline="middle";c.fillText(String(db),left-5,y)
                }
                var hp=mixer.output(cross.out,2),lp=mixer.output(cross.out,3)
                var hs=mixer.output(cross.out,5),ls=mixer.output(cross.out,6)
                c.strokeStyle="#6fe4cf";c.lineWidth=2.5;c.lineJoin="round";c.beginPath()
                for(var px=left;px<=right;px+=2){
                    var f=20*Math.pow(1000,(px-left)/(right-left))
                    var hi=1/Math.sqrt(1+Math.pow(hp/f,hs/3))
                    var lo=1/Math.sqrt(1+Math.pow(f/lp,ls/3))
                    var db=Math.max(-60,20*Math.log(Math.max(.0001,hi*lo))/Math.LN10)
                    var y=top+(-db/60)*(bottom-top)
                    if(px===left)c.moveTo(px,y);else c.lineTo(px,y)
                }
                c.stroke()
            }
        }
        onRevisionChanged:repaint.restart()
        onOutChanged:repaint.restart()
        Timer {id:repaint;interval:80;repeat:false;onTriggered:cv.requestPaint()}
    }
    // Eight-band parametric response: same RBJ peaking transfer function as dsp.h.
    component EqGraph: Item {
        id: eg
        property int channel: 0
        property bool isBus: false
        property bool isOutput: false
        property int band: 0
        property int revision: mixer.parameterRevision
        property bool draggingPoint:false
        function gain(b){if(draggingPoint && b===band)return pendingGain;return isOutput?mixer.outputEq(channel,b):(isBus?mixer.busEq(channel,b):mixer.eq(channel,b))}
        function freq(b){if(draggingPoint && b===band)return pendingFreq;return isOutput?mixer.outputEqFreq(channel,b):(isBus?mixer.busEqFreq(channel,b):mixer.eqFreq(channel,b))}
        function q(b){return isOutput?mixer.outputEqQ(channel,b):(isBus?mixer.busEqQ(channel,b):mixer.eqQ(channel,b))}
        function setGain(b,v){if(isOutput)mixer.setOutputEq(channel,b,v);else if(isBus)mixer.setBusEq(channel,b,v);else mixer.setEq(channel,b,v)}
        function setFreq(b,v){if(isOutput)mixer.setOutputEqFreq(channel,b,v);else if(isBus)mixer.setBusEqFreq(channel,b,v);else mixer.setEqFreq(channel,b,v)}
        function setQ(b,v){if(isOutput)mixer.setOutputEqQ(channel,b,v);else if(isBus)mixer.setBusEqQ(channel,b,v);else mixer.setEqQ(channel,b,v)}
        function xFreq(x){return 20*Math.pow(1000,Math.max(0,Math.min(1,x/Math.max(1,width))))}
        function freqX(f){return width*Math.log(Math.max(20,f)/20)/Math.log(1000)}
        function gainY(g){return height*(.5-Math.max(-root.eqVerticalRange,Math.min(root.eqVerticalRange,g))/(2*root.eqVerticalRange))}
        function yGain(y){return Math.max(-18,Math.min(18,(.5-y/height)*2*root.eqVerticalRange))}
        onRevisionChanged: {if(visible && !draggingPoint) eqDirty=true}
        Timer {id:redraw;interval:0;repeat:false;onTriggered:if(eg.visible && !eg.draggingPoint)plot.requestPaint()}
        onChannelChanged: redraw.restart()
        onIsBusChanged: redraw.restart()
        onBandChanged: redraw.restart()
        Connections {
            target:root
            function onEqVerticalRangeChanged(){plot.requestPaint()}
        }
        property real pendingFreq: 1000
        property real pendingGain: 0
        // Coalesce disk-backed parameter writes while dragging. The EQ handle
        // follows the pointer immediately, while DSP commits at most ~8/s.
        Timer {id:graphCommit;interval:125;repeat:true;running:eg.draggingPoint;onTriggered:{eg.setFreq(eg.band,eg.pendingFreq);eg.setGain(eg.band,eg.pendingGain)}}
        property bool eqDirty:true
        Timer {id:eqRepaint;interval:33;repeat:true;running:eg.visible;onTriggered:{if(eg.eqDirty){eg.eqDirty=false;plot.requestPaint()}}}
        Rectangle {anchors.fill:parent;radius:5;color:"#071726";border.color:"#42647a"}
        SpectrumOverlay {anchors.fill:parent;opacity:.95;z:0}
        // Analyzer dBFS scale (right), independent of the EQ gain scale (left).
        // FFT is normalized to full scale; RTA shows band power in dBFS-like
        // units, not calibrated acoustic dB SPL.
        Item {
            z:8
            width:45
            anchors.right:parent.right
            anchors.top:parent.top
            anchors.bottom:parent.bottom
            Rectangle {anchors.fill:parent;color:"#071726";opacity:.77}
            Repeater {
                model:5
                delegate:Text {
                    required property int index
                    readonly property real graphBottom: parent.height-20
                    readonly property real topLine: 8
                    text:(index===0?"0":String(-Math.round(root.analyzerRange*index/4)))+(index===0?" dBFS":"")
                    x:2
                    y:Math.max(0,Math.min(parent.height-height,topLine+(graphBottom-topLine)*index/4-height/2))
                    color:"#ffce7a"
                    font.pixelSize:10
                    font.bold:index===0
                    style:Text.Outline
                    styleColor:"#071726"
                }
            }
        }

        // Logarithmic ruler: 20, 200, 2k, 20k are equally spaced decades.
        // Both FFT and RTA use the same frequency-to-X mapping as the EQ.
        // High-contrast frequency ruler, aligned to log grid and EQ handles.
        Repeater {
            model: [20,50,100,200,500,1000,2000,5000,10000,20000]
            delegate: Text {
                required property var modelData
                text: modelData>=1000 ? (modelData/1000)+"k" : modelData+""
                x: Math.max(2,Math.min(eg.width-width-2,eg.freqX(modelData)-width/2))
                y: eg.height-height-3
                z: 5
                color: "#e1eaf4"
                style: Text.Outline
                styleColor: "#071726"
                font.pixelSize: 11
                font.bold: true
            }
        }
        Canvas {id:plot;anchors.fill:parent;antialiasing:true;renderTarget:Canvas.Image;z:1
            onPaint: {
                var ctx=getContext("2d"),w=width,h=height;ctx.clearRect(0,0,w,h)
                ctx.strokeStyle="#233f55";ctx.lineWidth=1
                for(var k=0;k<=6;k++){
                    var yy=12+(h-38)*k/6
                    ctx.beginPath();ctx.moveTo(0,yy);ctx.lineTo(w,yy);ctx.stroke()
                    ctx.fillStyle="#9db5c8";ctx.font="10px sans-serif";ctx.textAlign="left"
                    ctx.fillText(String(Math.round(root.eqVerticalRange*(1-k/3))),4,yy-3)
                }
                var hz=[20,50,100,200,500,1000,2000,5000,10000,20000]
                for(var k=0;k<hz.length;k++){var xx=eg.freqX(hz[k]);ctx.beginPath();ctx.moveTo(xx,0);ctx.lineTo(xx,h);ctx.stroke()}
                ctx.strokeStyle="#56dff1";ctx.lineWidth=3;ctx.lineJoin="round";ctx.beginPath()
                var coeff=[]
                for(var b=0;b<8;b++){
                    var fs=48000,f0=eg.freq(b),g=eg.gain(b),qv=Math.max(.2,eg.q(b)),a=Math.pow(10,g/40)
                    var omega=2*Math.PI*f0/fs,alpha=Math.sin(omega)/(2*qv),cs=Math.cos(omega),a0=1+alpha/a
                    coeff.push([(1+alpha*a)/a0,-2*cs/a0,(1-alpha*a)/a0,-2*cs/a0,(1-alpha/a)/a0])
                }
                for(var px=0;px<=w;px+=8){var f=eg.xFreq(px),total=0
                    var z=2*Math.PI*f/48000,cos=Math.cos(z),sin=Math.sin(z),cos2=Math.cos(2*z),sin2=Math.sin(2*z)
                    for(var b=0;b<8;b++){
                        var c=coeff[b]
                        var nr=c[0]+c[1]*cos+c[2]*cos2,ni=-c[1]*sin-c[2]*sin2
                        var dr=1+c[3]*cos+c[4]*cos2,di=-c[3]*sin-c[4]*sin2
                        total+=10*Math.log((nr*nr+ni*ni)/Math.max(1e-20,dr*dr+di*di))/Math.LN10
                    }
                    var py=eg.gainY(total);if(px===0)ctx.moveTo(px,py);else ctx.lineTo(px,py)
                }
                ctx.stroke()
                var colors=["#f6c45e","#e98c60","#d873ba","#ac8de6","#67b1fa","#56d9d5","#75df8b","#e5dd6a"]
                for(var b=0;b<8;b++){var x=eg.freqX(eg.freq(b)),y=eg.gainY(eg.gain(b));ctx.beginPath();ctx.arc(x,y,b===eg.band?9:6,0,2*Math.PI);ctx.fillStyle=colors[b];ctx.fill();ctx.strokeStyle=b===eg.band?"#fff":"#0b1827";ctx.lineWidth=2;ctx.stroke()}
            }
        }
        MouseArea {anchors.fill:parent;acceptedButtons:Qt.LeftButton
            onPressed:function(mouse){var closest=0,dmin=1e10;for(var b=0;b<8;b++){var dx=eg.freqX(eg.freq(b))-mouse.x,dy=eg.gainY(eg.gain(b))-mouse.y,d=dx*dx+dy*dy;if(d<dmin){dmin=d;closest=b}}root.selectedBand=closest;eg.pendingFreq=eg.freq(closest);eg.pendingGain=eg.gain(closest);eg.draggingPoint=true}
            onPositionChanged:function(mouse){if(pressed){eg.pendingFreq=eg.xFreq(mouse.x);eg.pendingGain=eg.yGain(mouse.y);eg.eqDirty=true}}
            onReleased:{if(eg.draggingPoint){graphCommit.stop();eg.setFreq(eg.band,eg.pendingFreq);eg.setGain(eg.band,eg.pendingGain);eg.draggingPoint=false;plot.requestPaint()}}
            onWheel:function(wheel){eg.setQ(eg.band,Math.max(.2,Math.min(12,eg.q(eg.band)*Math.pow(1.12,wheel.angleDelta.y/120))));wheel.accepted=true}
            onCanceled:{graphCommit.stop();eg.draggingPoint=false;plot.requestPaint()}
            onDoubleClicked: {eg.setGain(eg.band,0);eg.setQ(eg.band,1)}
        }
        Text {
            z:12
            anchors.left:parent.left
            anchors.leftMargin:8
            anchors.bottom:parent.bottom
            anchors.bottomMargin:20
            color:"#c9e6f1"
            font.pixelSize:11
            text: {
                // Explicit reactive dependencies on the spectrum and EQ revisions.
                var revision=eg.revision
                var values=mixer.spectrum
                var f=eg.freq(eg.band)
                var k=Math.max(0,Math.min(191,Math.round(191*Math.log(Math.max(20,f)/20)/Math.log(1000))))
                var measured=values.length>k?Number(values[k]).toFixed(1):"--"
                var gain=Number(eg.gain(eg.band)).toFixed(1)
                return "POST DSP " + Math.round(f) + " Hz: " + measured + " dBFS | EQ " + gain + " dB | PEAK " + Math.round(mixer.dominantHz) + " Hz (" + mixer.dominantDb.toFixed(1) + " dBFS)"
            }
        }
        // Overlay controls: EQ graph Y zoom and analyzer Y dynamic range.
        Row {
            z:20
            anchors.top:parent.top
            anchors.right:parent.right
            anchors.topMargin:8
            anchors.rightMargin:52
            spacing:4
            Btn {
                width:88;height:27
                text:"EQ ±"+root.eqVerticalRange
                onClicked:{
                    var ranges=[12,18,24,36,48,60,72]
                    root.eqVerticalRange=ranges[(ranges.indexOf(root.eqVerticalRange)+1)%ranges.length]
                }
            }
            Btn {
                width:89;height:27
                text:"RTA "+root.analyzerRange
                onClicked:{
                    var ranges=[30,45,60,90,120]
                    root.analyzerRange=ranges[(ranges.indexOf(root.analyzerRange)+1)%ranges.length]
                }
            }
        }
        // Keep this above the graph's drag MouseArea so taps toggle immediately.
        Btn {
            id:analyzerMode
            z:20
            x:8;y:8
            width:84;height:27
            text:root.analyzerRta?"RTA ▾":"FFT ▾"
            active:root.analyzerRta
            onClicked:root.analyzerRta=!root.analyzerRta
        }
    }
    component DynamicsGraph: Item {
        id: dg
        property int channel: 0
        property bool isBus: false
        property bool isOutput: false
        property int unit: 1
        property int revision: mixer.parameterRevision
        function val(key){return isOutput?mixer.outputDynamics(channel,unit,key):(isBus?mixer.busDynamics(channel,unit,key):mixer.dynamics(channel,unit,key))}
        function setVal(key,v){if(isOutput)mixer.setOutputDynamics(channel,unit,key,v);else if(isBus)mixer.setBusDynamics(channel,unit,key,v);else mixer.setDynamics(channel,unit,key,v)}
        function xp(db){return width*(db+60)/60}
        function ycoord(db){return height*(1-(db+60)/60)}
        onRevisionChanged: {if(visible) dynRedraw.restart()}
        Timer {id:dynRedraw;interval:100;repeat:false;onTriggered:if(dg.visible)cv.requestPaint()}
        onChannelChanged: dynRedraw.restart()
        onUnitChanged: dynRedraw.restart()
        Rectangle {anchors.fill:parent;color:"#071726";radius:5;border.color:"#42647a"}
        Canvas {id:cv;anchors.fill:parent;onPaint:{var c=getContext("2d"),w=width,h=height;c.clearRect(0,0,w,h);c.strokeStyle="#29485e";c.lineWidth=1
            for(var k=0;k<=6;k++){var x=k*w/6,y=k*h/6;c.beginPath();c.moveTo(x,0);c.lineTo(x,h);c.moveTo(0,y);c.lineTo(w,y);c.stroke()}
            c.strokeStyle="#647c8f";c.setLineDash([4,4]);c.beginPath();c.moveTo(0,h);c.lineTo(w,0);c.stroke();c.setLineDash([])
            c.strokeStyle="#51d9ee";c.lineWidth=3;c.beginPath();var th=dg.val("threshold"),ratio=Math.max(1,dg.val("ratio"));
            for(var x=0;x<=w;x+=2){var input=-60+60*x/w,output=input;if(dg.unit===0){if(input<th)output=-60}else if(input>th)output=th+(input-th)/ratio
                var y=dg.ycoord(output);if(x===0)c.moveTo(x,y);else c.lineTo(x,y)}c.stroke()
            c.fillStyle="#ffbb65";c.beginPath();c.arc(dg.xp(th),dg.ycoord(th),7,0,Math.PI*2);c.fill()
        }}
        // Filled reduction area between 1:1 and actual transfer curve, up to measured input.
        // Driven by live meter level; Canvas redraw is capped at 10 Hz.
        Canvas {
            id:activeCompression
            anchors.fill:parent
            z:1
            property real measured: dg.isBus ? (dg.channel===4 ? Math.max(mixer.outMeterL,mixer.outMeterR) : Number(mixer.auxMeters[dg.channel])) : Number(mixer.channelMeters[dg.channel])
            Timer {interval:100;running:activeCompression.visible;repeat:true;onTriggered:activeCompression.requestPaint()}
            onPaint: {
                var c=getContext("2d"),w=width,h=height
                c.clearRect(0,0,w,h)
                if(w<=0||h<=0||dg.unit===0)return
                var threshold=Number(dg.val("threshold")),ratio=Math.max(1,Number(dg.val("ratio")))
                var input=Math.max(-60,Math.min(0,activeCompression.measured))
                if(input<=threshold || ratio<=1)return
                var x0=dg.xp(threshold),x1=dg.xp(input)
                var compressed=threshold+(input-threshold)/ratio
                c.beginPath()
                c.moveTo(x0,dg.ycoord(threshold))
                c.lineTo(x1,dg.ycoord(input))
                c.lineTo(x1,dg.ycoord(compressed))
                c.closePath()
                c.fillStyle="rgba(255,166,74,0.28)"
                c.fill()
                c.strokeStyle="rgba(255,193,100,0.8)";c.lineWidth=1;c.stroke()
            }
        }
        Text {
            z:3
            anchors.top:parent.top;anchors.right:parent.right;anchors.margins:7
            text:"IN "+Number(activeCompression.measured).toFixed(1)+" dBFS"
            color:"#ffc16b";font.pixelSize:10
        }
        Rectangle {
            width:10;height:10;radius:5;color:"#ffcb64";border.color:"#fff";z:2
            property real measured: dg.isBus ? (dg.channel===4 ? Math.max(mixer.outMeterL,mixer.outMeterR) : Number(mixer.auxMeters[dg.channel])) : Number(mixer.channelMeters[dg.channel])
            x:Math.max(0,Math.min(parent.width-width,dg.xp(measured)-width/2))
            y:Math.max(0,Math.min(parent.height-height,dg.ycoord(measured-Math.max(0,dg.isBus ? (dg.channel===4 ? mixer.mainGR : Number(mixer.auxGR[dg.channel])) : Number(mixer.channelGR[dg.channel])))-height/2))
        }
        Timer {id:dynCommit;interval:60;repeat:false;onTriggered:{dg.setVal("threshold",dg.pendingThreshold);if(dg.unit!==0)dg.setVal("ratio",dg.pendingRatio)}}
        property real pendingThreshold:-18
        property real pendingRatio:2
        MouseArea {anchors.fill:parent;onPositionChanged:function(mouse){if(pressed){dg.pendingThreshold=Math.max(-60,Math.min(0,-60+60*mouse.x/width));dg.pendingRatio=Math.max(1,Math.min(20,1+(height-mouse.y)/height*19));dynCommit.restart()}}
            onReleased:{if(dynCommit.running){dynCommit.stop();dg.setVal("threshold",dg.pendingThreshold);if(dg.unit!==0)dg.setVal("ratio",dg.pendingRatio)}}
            onDoubleClicked:{dg.setVal("threshold",dg.unit===1?-18:-1);dg.setVal("ratio",dg.unit===1?2:20)}
        }
    }
    ColumnLayout {
        anchors.fill:parent; spacing:0
        Rectangle {Layout.fillWidth:true; Layout.preferredHeight:48; color:"#0c1e2e"; border.color:root.borderCol
            RowLayout {anchors.fill:parent; anchors.leftMargin:13; anchors.rightMargin:13
                Column {spacing:0
                    Text {text:"STORM MIX  NATIVE"; color:root.cyan; font.pixelSize:20; font.bold:true}
                    Text {text:"8 INPUT · 4 AUX · TOUCH 1024×600"; color:"#a2bfd3"; font.pixelSize:10}
                }
                Item {Layout.fillWidth:true}
                Rectangle {
                    implicitWidth:bankLabel.implicitWidth+20
                    implicitHeight:31
                    radius:5
                    color:"#12394b"
                    border.color:root.cyan
                    Text {
                        id:bankLabel
                        anchors.centerIn:parent
                        text:"BCF " + (mixer.midiBank+1) + "  ·  " + root.bcfBankName
                        color:"#e0faff"
                        font.pixelSize:11
                        font.bold:true
                    }
                }
                Text {text:mixer.status; color:"#ffc078"; font.pixelSize:11}
            }
        }
        Rectangle {Layout.fillWidth:true; Layout.preferredHeight:44; color:"#0b1827"; border.color:root.borderCol
            Flickable {anchors.fill:parent;anchors.margins:4;contentWidth:tabRow.width;contentHeight:height;clip:true;boundsBehavior:Flickable.StopAtBounds
                Row {id:tabRow;spacing:3;height:parent.height
                    Repeater {model:root.tabs
                        Btn {width:Math.max(80,modelData.length*10+18);height:35;text:modelData;active:root.page===index;onClicked:root.page=index}
                    }
                }
            }
        }
        StackLayout {
            id:pages; currentIndex:root.page; Layout.fillWidth:true; Layout.fillHeight:true
            // 0 MIXER
            Item {
                RowLayout {anchors.fill:parent; anchors.margins:7; spacing:4
                    Repeater {model:8
                        Strip {idx:index; channelData:mixer.channels[index]; Layout.fillWidth:true; Layout.fillHeight:true; Layout.minimumWidth:0
                            onEdit: {mixer.selected=idx; root.page=2}}
                    }
                    Rack {Layout.preferredWidth:96; Layout.fillHeight:true; border.color:"#a47c43"
                        ColumnLayout {anchors.fill:parent; anchors.margins:6; spacing:5
                            Text {text:"MAIN L/R"; color:"#ffd08b"; font.bold:true; font.pixelSize:12; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter}
                            Item {id:mainArea; Layout.fillWidth:true; Layout.fillHeight:true
                                Row {anchors.centerIn:parent;spacing:3
                                    VFader {id:masterFader;height:Math.max(90,mainArea.height-6);value:mixer.master;onUserChanged:function(v){mixer.master=v}}
                                    VUMeter {height:Math.max(90,mainArea.height-6);level:mixer.monitorMeter}
                                    GRBar {height:Math.max(90,mainArea.height-6);reduction:mixer.mainGR;barColor:"#ffbf66"}
                                    GRBar {height:Math.max(90,mainArea.height-6);reduction:mixer.limiterGR;barColor:"#f06a65"}
                                }
                            }
                            RowLayout {Layout.fillWidth:true;spacing:2
                                Text {text:root.db(masterFader.liveValue); color:root.cyan; font.bold:true;Layout.fillWidth:true}
                                Btn {text:"↺";Layout.preferredWidth:24;onClicked:mixer.master=0}
                            }
                            Text {text:(mixer.pflLabel.length?mixer.pflLabel+" ":"MAIN ")+mixer.monitorMeter.toFixed(1)+" dBFS";color:"#8bc9e4";font.pixelSize:10;Layout.alignment:Qt.AlignHCenter}
                            Text {text:"GR "+mixer.mainGR.toFixed(1)+" / LIM "+mixer.limiterGR.toFixed(1);color:"#ffb65d";font.pixelSize:9;Layout.alignment:Qt.AlignHCenter}
                            Btn {text:"MASTER DSP"; Layout.fillWidth:true; onClicked:root.page=10}
                        }
                    }
                }
            }
            // 1 AUX
            Item {
                ColumnLayout {anchors.fill:parent; anchors.margins:9; spacing:6
                    RowLayout {Layout.fillWidth:true
                        Text {text:"AUX MIXER · BUS 1–4"; color:root.cyan; font.pixelSize:16; font.bold:true}
                        Item {Layout.fillWidth:true}
                        Text {text:"Send levels: ROUTING"; color:"#a5c2d4"; font.pixelSize:11}
                    }
                    RowLayout {Layout.fillWidth:true; Layout.fillHeight:true; spacing:8
                        Repeater {model:4
                            Strip {idx:index; channelData:mixer.auxes[index]; isAux:true; Layout.fillWidth:true; Layout.fillHeight:true; onEdit:{root.selectedAux=idx;root.page=9}}
                        }
                    }
                }
            }
            // 2 CHANNEL / EQ
            Item {ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                RowLayout {Layout.fillWidth:true
                    Text {text:"CHANNEL · 8-BAND PARAMETRIC EQ";color:root.cyan;font.bold:true;font.pixelSize:16}
                    ComboBox {model:["CH 1","CH 2","CH 3","CH 4","CH 5","CH 6","CH 7","CH 8"];currentIndex:mixer.selected;onActivated:function(i){mixer.selected=i}}
                    Btn {text:"LINK "+(Math.floor(mixer.selected/2)*2+1)+"/"+(Math.floor(mixer.selected/2)*2+2);active:(mixer.parameterRevision, mixer.linked(Math.floor(mixer.selected/2)));onClicked:mixer.setLinked(Math.floor(mixer.selected/2),!mixer.linked(Math.floor(mixer.selected/2)))}
                    Item {Layout.fillWidth:true}
                    Btn {text:"DYNAMICS →";onClicked:root.page=3}
                }
                RowLayout {Layout.fillWidth:true;Layout.fillHeight:true;spacing:7
                    Rack {Layout.fillWidth:true;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:6
                            EqGraph {id:channelGraph;channel:mixer.selected;band:root.selectedBand;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:175}
                            RowLayout {Layout.fillWidth:true;spacing:3
                                Repeater {model:8;Btn {text:String(index+1);active:root.selectedBand===index;Layout.fillWidth:true;Layout.minimumWidth:0;onClicked:root.selectedBand=index}}
                            }
                            Text {text:"Curva EQ + spettro MAIN (verde) · trascina FREQ/GAIN · rotella Q";color:"#a8c6d8";font.pixelSize:10}
                        }
                    }
                    Rack {Layout.preferredWidth:285;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                            Text {text:"BANDA "+(root.selectedBand+1);color:"#ffcb6e";font.bold:true}
                            KnobSlider {label:"FREQ (Hz)";from:20;to:20000;decimals:0;resetValue:1000;value:(mixer.parameterRevision, mixer.eqFreq(mixer.selected,root.selectedBand));Layout.fillWidth:true;onChanged:function(v){mixer.setEqFreq(mixer.selected,root.selectedBand,v)}}
                            KnobSlider {label:"GAIN (dB)";from:-18;to:18;value:(mixer.parameterRevision, mixer.eq(mixer.selected,root.selectedBand));Layout.fillWidth:true;onChanged:function(v){mixer.setEq(mixer.selected,root.selectedBand,v)}}
                            KnobSlider {label:"Q / WIDTH";from:.2;to:12;resetValue:1;value:(mixer.parameterRevision, mixer.eqQ(mixer.selected,root.selectedBand));Layout.fillWidth:true;onChanged:function(v){mixer.setEqQ(mixer.selected,root.selectedBand,v)}}
                            KnobSlider {label:"DIGITAL GAIN";from:-24;to:24;value:Number(mixer.channels[mixer.selected].gain);Layout.fillWidth:true;onChanged:function(v){mixer.setChannel(mixer.selected,"gain",v)}}
                            KnobSlider {label:"PAN L/R";from:-100;to:100;value:Number(mixer.channels[mixer.selected].pan);Layout.fillWidth:true;onChanged:function(v){mixer.setChannel(mixer.selected,"pan",v)}}
                            Item {Layout.fillHeight:true}
                        }
                    }
                }
            }}
            // 3 DYNAMICS
            Item {ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:7
                RowLayout {Layout.fillWidth:true
                    Text {text:"CHANNEL DYNAMICS";color:root.cyan;font.bold:true;font.pixelSize:17}
                    ComboBox {model:["CH 1","CH 2","CH 3","CH 4","CH 5","CH 6","CH 7","CH 8"];currentIndex:mixer.selected;onActivated:function(i){mixer.selected=i}}
                    Item {Layout.fillWidth:true}
                    Btn {text:root.channelDynOn(mixer.selected,root.dynUnit)?"● ON":"○ BYPASS";active:root.channelDynOn(mixer.selected,root.dynUnit);onClicked:mixer.setDynamicsEnabled(mixer.selected,root.dynUnit,!root.channelDynOn(mixer.selected,root.dynUnit))}
                    Repeater {model:["GATE","COMP","LIMITER"];Btn {text:modelData;active:root.dynUnit===index;onClicked:root.dynUnit=index}}
                }
                RowLayout {Layout.fillWidth:true;Layout.fillHeight:true;spacing:8
                    Rack {Layout.fillWidth:true;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:10;spacing:5
                            DynamicsGraph {channel:mixer.selected;unit:root.dynUnit;Layout.fillWidth:true;Layout.fillHeight:true}
                            Text {text:"Trascina per THRESHOLD / RATIO · GR CH "+Number(mixer.channelGR[mixer.selected]).toFixed(1)+" dB";color:"#ffbc65";font.pixelSize:11}
                        }
                    }
                    Rack {Layout.preferredWidth:330;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:10;spacing:8
                            Repeater {model:["threshold","ratio","attack","release"]
                                KnobSlider {property string key:modelData;label:key.toUpperCase();Layout.fillWidth:true
                                    from:key==="threshold"?-60:key==="ratio"?1:key==="attack"?1:10
                                    to:key==="threshold"?0:key==="ratio"?20:key==="attack"?200:1000
                                    resetValue:key==="threshold"?-18:key==="ratio"?2:key==="attack"?10:120
                                    value:(mixer.parameterRevision, mixer.dynamics(mixer.selected,root.dynUnit,key))
                                    onChanged:function(v){mixer.setDynamics(mixer.selected,root.dynUnit,key,v)}
                                }
                            }
                            Item {Layout.fillHeight:true}
                        }
                    }
                }
            }}
            // 4 ROUTING
            Item {
                ColumnLayout {anchors.fill:parent; anchors.margins:12; spacing:10
                    RowLayout {Layout.fillWidth:true
                        Text {text:"ROUTING · INPUT SENDS TO AUX"; color:root.cyan; font.pixelSize:17; font.bold:true}
                        Item {Layout.fillWidth:true}
                        Text {text:"INPUT"; color:"#d9eaf7"}
                        ComboBox {id:routeChannel; model:["CH 1","CH 2","CH 3","CH 4","CH 5","CH 6","CH 7","CH 8"]}
                    }
                    Rack {Layout.fillWidth:true; Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent; anchors.margins:14; spacing:16
                            Repeater {model:4
                                KnobSlider {id:sendRow; property int bus:index; label:"SEND TO AUX "+(index+1)+" (dB)"; from:-60; to:10
                                    value: (mixer.parameterRevision, mixer.send(routeChannel.currentIndex,bus)); Layout.fillWidth:true
                                    onChanged:function(v){mixer.setSend(routeChannel.currentIndex,bus,v)}}
                            }
                            Rectangle {Layout.fillWidth:true;Layout.preferredHeight:1;color:"#3a5870"}
                            Text {text:"ROUTING USCITE FISICHE 1/2";color:root.cyan;font.bold:true;font.pixelSize:13}
                            Repeater {model:2
                                RowLayout {Layout.fillWidth:true;spacing:12
                                    Text {text:"OUT "+(index+1);color:"#fff";Layout.preferredWidth:65}
                                    ComboBox {property int outIndex:index;Layout.fillWidth:true
                                        model:["MAIN L/R (legacy)","MAIN MONO","AUX 1","AUX 2","AUX 3","AUX 4","MUTE","MAIN L","MAIN R"]
                                        currentIndex:(mixer.parameterRevision,mixer.mainRoute(outIndex))
                                        onActivated:function(i){mixer.setMainRoute(outIndex,i)}
                                    }
                                }
                            }
                            Item {Layout.fillHeight:true}
                            Text {text:"2x2: AUX sempre sommati al MAIN; routing fisico disponibile su OUT 1/2."; color:"#ffbe72"; font.pixelSize:11}
                        }
                    }
                }
            }
            // 5 OUTPUT: physical processing after bus DSP. AUX routes bypass output EQ/DYN.
            Item {
                ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                    RowLayout {Layout.fillWidth:true
                        Text {text:"OUTPUT · CROSSOVER / DSP";color:root.cyan;font.bold:true;font.pixelSize:16}
                        Item {Layout.fillWidth:true}
                        Text {text:mixer.selectedOutput<2?"ACTIVE · PHYSICAL OUT":"RESERVED · NO PHYSICAL I/O";color:mixer.selectedOutput<2?"#6cdeb1":"#ffbc69";font.pixelSize:11}
                    }
                    RowLayout {Layout.fillWidth:true;spacing:3
                        Repeater {model:8;Btn {text:"OUT "+(index+1);active:mixer.selectedOutput===index;Layout.fillWidth:true;Layout.minimumWidth:0
                            onClicked:{mixer.selectedOutput=index;if(root.page===5)mixer.setSpectrumSource(13+Math.min(index,1))}}}
                    }
                    RowLayout {Layout.fillWidth:true;spacing:5
                        Repeater {model:["CROSSOVER + LEVEL","PARAMETRIC EQ · 8 BANDS","GATE / COMP / LIMIT"]
                            Btn {text:modelData;active:root.selectedOutputView===index;Layout.fillWidth:true;Layout.minimumWidth:0
                                onClicked:root.selectedOutputView=index}
                        }
                    }
                    ScrollView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;ScrollBar.horizontal.policy:ScrollBar.AlwaysOff
                        ColumnLayout {width:Math.max(760,root.width-35);spacing:5
                            RowLayout {visible:root.selectedOutputView===0;Layout.fillWidth:true;spacing:7
                            Rack {Layout.fillWidth:true;Layout.preferredHeight:280
                                ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                                    RowLayout {Layout.fillWidth:true
                                        Text {text:"CROSSOVER · HPF / LPF";font.bold:true;color:"#75d8ed"}
                                        Item {Layout.fillWidth:true}
                                        Text {text:"12 / 24 dB per octave · Butterworth";font.pixelSize:10;color:"#b7cbd9"}
                                    }
                                    CrossoverGraph {out:mixer.selectedOutput;Layout.fillWidth:true;Layout.preferredHeight:115}
                                    RowLayout {Layout.fillWidth:true;spacing:5
                                        KnobSlider {label:"HPF Hz";from:20;to:2000;decimals:0;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.output(mixer.selectedOutput,2));onChanged:function(v){mixer.setOutput(mixer.selectedOutput,2,v)}}
                                        KnobSlider {label:"LPF Hz";from:100;to:20000;decimals:0;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.output(mixer.selectedOutput,3));onChanged:function(v){mixer.setOutput(mixer.selectedOutput,3,v)}}
                                    }
                                    RowLayout {Layout.fillWidth:true;spacing:6
                                        ColumnLayout {Layout.fillWidth:true;spacing:2;Text {text:"HP SLOPE";color:"#9ec3d9";font.pixelSize:10}
                                            ComboBox {Layout.fillWidth:true;palette.text:"#102738";model:["12 dB/oct","24 dB/oct"];currentIndex:mixer.output(mixer.selectedOutput,5)>=24?1:0;onActivated:function(i){mixer.setOutput(mixer.selectedOutput,5,i?24:12)}}}
                                        ColumnLayout {Layout.fillWidth:true;spacing:2;Text {text:"LP SLOPE";color:"#9ec3d9";font.pixelSize:10}
                                            ComboBox {Layout.fillWidth:true;palette.text:"#102738";model:["12 dB/oct","24 dB/oct"];currentIndex:mixer.output(mixer.selectedOutput,6)>=24?1:0;onActivated:function(i){mixer.setOutput(mixer.selectedOutput,6,i?24:12)}}}
                                    }
                                }
                            }
                            Rack {Layout.fillWidth:true;Layout.preferredHeight:280
                                ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                                    Text {text:"OUTPUT CONTROL · LEVEL / DELAY / LIMIT";color:root.cyan;font.bold:true;font.pixelSize:11}
                                    KnobSlider {label:"TRIM (dB)";from:-24;to:24;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.output(mixer.selectedOutput,0));onChanged:function(v){mixer.setOutput(mixer.selectedOutput,0,v)}}
                                    KnobSlider {label:"DELAY (ms)";from:0;to:500;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.output(mixer.selectedOutput,1));onChanged:function(v){mixer.setOutput(mixer.selectedOutput,1,v)}}
                                    KnobSlider {label:"LIMITER CEILING";from:-24;to:-.1;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.output(mixer.selectedOutput,4));onChanged:function(v){mixer.setOutput(mixer.selectedOutput,4,v)}}
                                }
                            }
                            }
                            RowLayout {visible:root.selectedOutputView!==0;Layout.fillWidth:true;spacing:7
                            Rack {visible:root.selectedOutputView===1;Layout.fillWidth:true;Layout.preferredHeight:325
                                ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                                    RowLayout {Layout.fillWidth:true
                                        Text {text:"OUTPUT EQ · 8 BANDS";color:root.cyan;font.bold:true}
                                        Item {Layout.fillWidth:true}
                                        Text {text:"AUX → EQ BYPASS";color:"#ffbd69";font.pixelSize:10}
                                    }
                                    EqGraph {isOutput:true;channel:mixer.selectedOutput;band:root.selectedOutputBand;Layout.fillWidth:true;Layout.preferredHeight:205}
                                    RowLayout {Layout.fillWidth:true;spacing:3
                                        Repeater {model:8;Btn {text:"B"+(index+1);active:root.selectedOutputBand===index;Layout.fillWidth:true;onClicked:root.selectedOutputBand=index}}
                                    }
                                    RowLayout {Layout.fillWidth:true;spacing:4
                                        KnobSlider {label:"FREQ";from:20;to:20000;decimals:0;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.outputEqFreq(mixer.selectedOutput,root.selectedOutputBand));onChanged:function(v){mixer.setOutputEqFreq(mixer.selectedOutput,root.selectedOutputBand,v)}}
                                        KnobSlider {label:"GAIN";from:-18;to:18;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.outputEq(mixer.selectedOutput,root.selectedOutputBand));onChanged:function(v){mixer.setOutputEq(mixer.selectedOutput,root.selectedOutputBand,v)}}
                                        KnobSlider {label:"Q";from:.2;to:12;Layout.fillWidth:true;value:(mixer.parameterRevision,mixer.outputEqQ(mixer.selectedOutput,root.selectedOutputBand));onChanged:function(v){mixer.setOutputEqQ(mixer.selectedOutput,root.selectedOutputBand,v)}}
                                    }
                                }
                            }
                            Rack {visible:root.selectedOutputView===2;Layout.fillWidth:true;Layout.preferredHeight:325
                                ColumnLayout {anchors.fill:parent;anchors.margins:9;spacing:5
                                    RowLayout {Layout.fillWidth:true
                                        Text {text:"OUTPUT DYNAMICS";color:root.cyan;font.bold:true}
                                        Item {Layout.fillWidth:true}
                                        Btn {text:root.outputDynOn(mixer.selectedOutput,root.selectedOutputDyn)?"● ON":"○ BYPASS";active:root.outputDynOn(mixer.selectedOutput,root.selectedOutputDyn);onClicked:mixer.setOutputDynamicsEnabled(mixer.selectedOutput,root.selectedOutputDyn,!root.outputDynOn(mixer.selectedOutput,root.selectedOutputDyn))}
                                        Repeater {model:["GATE","COMP","LIMIT"];Btn {text:modelData;active:root.selectedOutputDyn===index;onClicked:root.selectedOutputDyn=index}}
                                    }
                                    DynamicsGraph {isOutput:true;channel:mixer.selectedOutput;unit:root.selectedOutputDyn;Layout.fillWidth:true;Layout.preferredHeight:205}
                                    RowLayout {Layout.fillWidth:true;spacing:5
                                        Repeater {model:["threshold","ratio","attack","release"]
                                            KnobSlider {property string k:modelData;label:k.toUpperCase();Layout.fillWidth:true
                                                from:k==="threshold"?-70:k==="ratio"?1:1;to:k==="threshold"?0:k==="ratio"?30:k==="attack"?200:1000
                                                value:(mixer.parameterRevision,mixer.outputDynamics(mixer.selectedOutput,root.selectedOutputDyn,k))
                                                onChanged:function(v){mixer.setOutputDynamics(mixer.selectedOutput,root.selectedOutputDyn,k,v)}}}
                                    }
                                }
                            }
                            }
                            Text {text:"OUT "+(mixer.selectedOutput+1)+"  •  "+(root.selectedOutputView===0?"Crossover, trim, delay e limiter":root.selectedOutputView===1?"EQ parametrico a 8 bande":"Dynamics: gate, compressore e limiter")+"  •  AUX DSP prioritario";font.pixelSize:11;color:"#ffbe72";wrapMode:Text.WordWrap;Layout.fillWidth:true}
                        }
                    }
                }
            }
            // 6 FX
            Item {
                ColumnLayout {anchors.fill:parent; anchors.margins:11; spacing:9
                    RowLayout {Layout.fillWidth:true
                        Text {text:"FX RACK · MAIN BUS";color:root.cyan;font.pixelSize:17;font.bold:true}
                        Item {Layout.fillWidth:true}
                        Text {text:"DELAY  /  REVERB  /  CHORUS";color:"#e7c2f9";font.pixelSize:11}
                    }
                    RowLayout {Layout.fillWidth:true; Layout.fillHeight:true; spacing:9
                        Repeater {model:["DELAY","REVERB","CHORUS"]
                            Rack {id:fxRack; property int unit:index; Layout.fillWidth:true; Layout.fillHeight:true
                                ColumnLayout {anchors.fill:parent; anchors.margins:13; spacing:15
                                    Text {text:modelData; color:"#f5caff"; font.pixelSize:16; font.bold:true}
                                    Repeater {model:["TIME / SIZE","FEEDBACK / DECAY","MIX"]
                                        KnobSlider {id:fxParam; property int param:index; label:modelData; from:0; to:100
                                            value: (mixer.parameterRevision, mixer.fx(fxRack.unit,param)); Layout.fillWidth:true
                                            onChanged:function(v){mixer.setFx(fxRack.unit,param,v)}}
                                    }
                                    Item {Layout.fillHeight:true}
                                }
                            }
                        }
                    }
                    Text {text:"Effetti sperimentali sul MAIN. MIX = 0 per bypass del singolo effetto. I controlli mantengono i preset SHOW."; color:"#ffbe72"; font.pixelSize:11;wrapMode:Text.WordWrap}
                }
            }
            // 7 METERS: compact 8+4+MAIN
            Item {ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:7
                Text {text:"METER BRIDGE · INPUT / AUX / MAIN";color:root.cyan;font.pixelSize:17;font.bold:true}
                RowLayout {Layout.fillWidth:true;Layout.fillHeight:true;spacing:4
                    Repeater {model:8
                        Rack {Layout.fillWidth:true;Layout.fillHeight:true
                            ColumnLayout {anchors.fill:parent;anchors.margins:5;spacing:4
                                Text {text:"CH "+(index+1);color:"#fff";font.pixelSize:11;Layout.alignment:Qt.AlignHCenter}
                                Row {Layout.alignment:Qt.AlignHCenter;Layout.fillHeight:true;spacing:3
                                    VUMeter {level:Number(mixer.channelMeters[index]);height:parent.height;width:20}
                                    GRBar {reduction:Number(mixer.channelGR[index]);height:parent.height;width:7}
                                }
                                Text {text:Number(mixer.channelMeters[index]).toFixed(0);color:root.cyan;font.pixelSize:10;Layout.alignment:Qt.AlignHCenter}
                                Text {text:"GR "+Number(mixer.channelGR[index]).toFixed(1);color:"#ffbc65";font.pixelSize:9;Layout.alignment:Qt.AlignHCenter}
                            }
                        }
                    }
                    Rectangle {Layout.preferredWidth:2;Layout.fillHeight:true;color:"#44647d"}
                    Repeater {model:4
                        Rack {Layout.fillWidth:true;Layout.fillHeight:true
                            ColumnLayout {anchors.fill:parent;anchors.margins:5;spacing:4
                                Text {text:"AUX "+(index+1);color:"#ffbd65";font.pixelSize:11;Layout.alignment:Qt.AlignHCenter}
                                Row {Layout.alignment:Qt.AlignHCenter;Layout.fillHeight:true;spacing:3
                                    VUMeter {level:Number(mixer.auxMeters[index]);height:parent.height;width:20}
                                    GRBar {reduction:Number(mixer.auxGR[index]);height:parent.height;width:7}
                                }
                                Text {text:Number(mixer.auxMeters[index]).toFixed(0);color:root.cyan;font.pixelSize:10;Layout.alignment:Qt.AlignHCenter}
                                Text {text:"GR "+Number(mixer.auxGR[index]).toFixed(1);color:"#ffbc65";font.pixelSize:9;Layout.alignment:Qt.AlignHCenter}
                            }
                        }
                    }
                    Rectangle {Layout.preferredWidth:2;Layout.fillHeight:true;color:"#44647d"}
                    Rack {Layout.preferredWidth:110;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:5;spacing:4
                            Text {text:"MAIN L/R";color:"#ffd089";font.bold:true;font.pixelSize:11}
                            RowLayout {Layout.fillHeight:true;Layout.alignment:Qt.AlignHCenter;spacing:5
                                VUMeter {level:mixer.outMeterL;Layout.fillHeight:true;width:23}
                                VUMeter {level:mixer.outMeterR;Layout.fillHeight:true;width:23}
                            }
                            Text {text:"GR "+mixer.mainGR.toFixed(1);color:"#ffbc65";font.pixelSize:10}
                            Text {text:"LIM "+mixer.limiterGR.toFixed(1);color:"#ff8b65";font.pixelSize:10}
                        }
                    }
                }
                Text {text:"Meter AUX reali nel DSP; su hardware 2x2 gli AUX sono ripiegati nel MAIN (non uscite fisiche separate).";color:"#ffbc65";font.pixelSize:10}
            }}
            // 8 SYSTEM
            Item {
                ColumnLayout {anchors.fill:parent; anchors.margins:18; spacing:13
                    Text {text:"SYSTEM · STORM MIX NATIVE"; color:root.cyan; font.pixelSize:19; font.bold:true}
                    Text {text:"Qt Quick 6 · 1024×600 · ALSA · 2×2 test (8×8 predisposto)"; color:"#d7eaf7"}
                    Text {text:mixer.status; color:mixer.audioRunning?"#43d6aa":"#ffbe72"; wrapMode:Text.Wrap; Layout.fillWidth:true}
                    RowLayout {spacing:9; Layout.fillWidth:true
                        Text {text:"INPUT ALSA:"; color:"#c6e7f7"; Layout.preferredWidth:110}
                        ComboBox {
                            id:inputSelect; Layout.fillWidth:true
                            model:mixer.audioDevices(); textRole:"name"; valueRole:"name"
                            enabled:!mixer.audioRunning; editable:true
                            Component.onCompleted: {for(var i=0;i<count;i++)if(model[i].name===mixer.audioInputDevice){currentIndex=i;break}}
                            onActivated:function(i){mixer.audioInputDevice=model[i].name}
                            onAccepted:{if(!mixer.audioRunning && editText.trim().length>0)mixer.audioInputDevice=editText.trim()}
                        }
                    }
                    RowLayout {spacing:9; Layout.fillWidth:true
                        Text {text:"OUTPUT ALSA:"; color:"#c6e7f7"; Layout.preferredWidth:110}
                        ComboBox {
                            id:outputSelect; Layout.fillWidth:true
                            model:mixer.audioDevices(); textRole:"name"; valueRole:"name"
                            enabled:!mixer.audioRunning; editable:true
                            Component.onCompleted: {for(var i=0;i<count;i++)if(model[i].name===mixer.audioOutputDevice){currentIndex=i;break}}
                            onActivated:function(i){mixer.audioOutputDevice=model[i].name}
                            onAccepted:{if(!mixer.audioRunning && editText.trim().length>0)mixer.audioOutputDevice=editText.trim()}
                        }
                        Btn {text:"AGGIORNA LISTA"; onClicked:{
                            inputSelect.model=mixer.audioDevices();outputSelect.model=mixer.audioDevices();
                            for(var i=0;i<inputSelect.count;i++)if(inputSelect.model[i].name===mixer.audioInputDevice)inputSelect.currentIndex=i
                            for(var j=0;j<outputSelect.count;j++)if(outputSelect.model[j].name===mixer.audioOutputDevice)outputSelect.currentIndex=j
                        }}
                    }
                    Text {text:"TEST: INPUT plughw:2,1  ·  OUTPUT plughw:0,0  ·  sorgente verso plughw:2,0"; color:"#a4bfd2"}
                    Text {text:"IN: " + mixer.audioInputDevice + "  ·  OUT: " + mixer.audioOutputDevice + "  ·  XRUN: " + mixer.audioXruns; color:"#85d6f4"}
                    Text {text:"Sample rate: " + (mixer.audioSampleRate||"—") + " Hz  ·  Blocco DSP: " + (mixer.audioBlockFrames||"—") + " frame"; color:"#85d6f4"}
                    Text {text:"INGRESSO L: "+mixer.inMeterL.toFixed(1)+" dBFS · R: "+mixer.inMeterR.toFixed(1)+" dBFS";color:"#43d6aa"}
                    Text {text:"USCITA L: "+mixer.outMeterL.toFixed(1)+" dBFS · R: "+mixer.outMeterR.toFixed(1)+" dBFS";color:"#ffbe72"}
                    RowLayout {spacing:10
                        Btn {text:"START AUDIO"; enabled:!mixer.audioRunning; onClicked:mixer.startAudio()}
                        Btn {text:"STOP AUDIO"; enabled:mixer.audioRunning; onClicked:mixer.stopAudio()}
                        Btn {text:"RIAVVIA MOTORE"; onClicked:mixer.restartAudio()}
                    }
                    RowLayout {
                        Text {text:"Meter dimostrativi"; color:"#fff"}
                        Switch {checked:mixer.demoMeters; onToggled:mixer.demoMeters=checked}
                    }
                    Btn {text:"SALVA PARAMETRI"; Layout.preferredWidth:230; onClicked:mixer.save()}
                    Text {text:"Input e output salvati con QSettings. Cambiare scheda solo a motore fermo."; color:"#a4bfd2"}
                    Text {text:"Stati: STOPPED / OPENING / RUNNING / ERROR. Per usare una scheda USB 2×2 scegliere plughw:X,Y. Rete, Wi-Fi e MIDI non ancora collegati."; color:"#ffbe72"; wrapMode:Text.Wrap; Layout.fillWidth:true}
                    Item {Layout.fillHeight:true}
                }
            }
            // 9 AUX DSP
            Item {ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:5
                RowLayout {Layout.fillWidth:true
                    Text {text:"AUX BUS DSP · EQ / DYNAMICS";color:root.cyan;font.bold:true;font.pixelSize:16}
                    ComboBox {model:["AUX 1","AUX 2","AUX 3","AUX 4"];currentIndex:root.selectedAux;onActivated:function(i){root.selectedAux=i}}
                    Item {Layout.fillWidth:true}
                    Repeater {model:["GATE","COMP","LIMITER"];Btn {text:modelData;active:root.busDynUnit===index;onClicked:root.busDynUnit=index}}
                }
                RowLayout {Layout.fillWidth:true;Layout.fillHeight:true;spacing:6
                    Rack {Layout.fillWidth:true;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:4
                            Text {text:"8-BAND EQ · trascina / rotella Q";color:root.cyan;font.pixelSize:11}
                            EqGraph {isBus:true;channel:root.selectedAux;band:root.selectedBand;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:135}
                            RowLayout {Layout.fillWidth:true;spacing:2;Repeater {model:8;Btn {text:String(index+1);active:root.selectedBand===index;Layout.fillWidth:true;Layout.minimumWidth:0;onClicked:root.selectedBand=index}}}
                            KnobSlider {label:"EQ GAIN";from:-18;to:18;value:(mixer.parameterRevision, mixer.busEq(root.selectedAux,root.selectedBand));onChanged:function(v){mixer.setBusEq(root.selectedAux,root.selectedBand,v)};Layout.fillWidth:true}
                            KnobSlider {label:"EQ FREQ";from:20;to:20000;decimals:0;value:(mixer.parameterRevision, mixer.busEqFreq(root.selectedAux,root.selectedBand));onChanged:function(v){mixer.setBusEqFreq(root.selectedAux,root.selectedBand,v)};Layout.fillWidth:true}
                            KnobSlider {label:"EQ Q";from:.2;to:12;resetValue:1;value:(mixer.parameterRevision, mixer.busEqQ(root.selectedAux,root.selectedBand));onChanged:function(v){mixer.setBusEqQ(root.selectedAux,root.selectedBand,v)};Layout.fillWidth:true}
                        }
                    }
                    Rack {Layout.fillWidth:true;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:4
                            RowLayout {Layout.fillWidth:true
                            Text {text:"DYNAMICS · GR "+Number(mixer.auxGR[root.selectedAux]).toFixed(1)+" dB";color:"#ffbd65";font.pixelSize:11}
                            Item {Layout.fillWidth:true}
                            Btn {text:root.busDynOn(root.selectedAux,root.busDynUnit)?"● ON":"○ BYPASS";active:root.busDynOn(root.selectedAux,root.busDynUnit);onClicked:mixer.setBusDynamicsEnabled(root.selectedAux,root.busDynUnit,!root.busDynOn(root.selectedAux,root.busDynUnit))}
                            Repeater {model:["G","C","L"];Btn {text:modelData;active:root.busDynUnit===index;onClicked:root.busDynUnit=index}}
                            }
                            DynamicsGraph {isBus:true;channel:root.selectedAux;unit:root.busDynUnit;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:120}
                            Repeater {model:["threshold","ratio","attack","release"]
                                KnobSlider {property string key:modelData;label:key.toUpperCase();Layout.fillWidth:true
                                    from:key==="threshold"?-60:key==="ratio"?1:key==="attack"?1:10
                                    to:key==="threshold"?0:key==="ratio"?20:key==="attack"?200:1000
                                    resetValue:key==="threshold"?-18:key==="ratio"?2:key==="attack"?10:120
                                    value:(mixer.parameterRevision, mixer.busDynamics(root.selectedAux,root.busDynUnit,key))
                                    onChanged:function(v){mixer.setBusDynamics(root.selectedAux,root.busDynUnit,key,v)}
                                }
                            }
                        }
                    }
                }
            }}
            // 10 MAIN DSP
            Item {ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:5
                RowLayout {Layout.fillWidth:true
                    Text {text:"MAIN L/R DSP · EQ / DYNAMICS";color:root.cyan;font.bold:true;font.pixelSize:16}
                    Item {Layout.fillWidth:true}
                    Repeater {model:["GATE","COMP","LIMITER"];Btn {text:modelData;active:root.busDynUnit===index;onClicked:root.busDynUnit=index}}
                }
                RowLayout {Layout.fillWidth:true;Layout.fillHeight:true;spacing:6
                    Rack {Layout.fillWidth:true;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:4
                            Text {text:"MAIN EQ · 8 BANDS";color:root.cyan;font.pixelSize:11}
                            EqGraph {isBus:true;channel:4;band:root.selectedBand;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:135}
                            RowLayout {Layout.fillWidth:true;spacing:2;Repeater {model:8;Btn {text:String(index+1);active:root.selectedBand===index;Layout.fillWidth:true;Layout.minimumWidth:0;onClicked:root.selectedBand=index}}}
                            KnobSlider {label:"EQ GAIN";from:-18;to:18;value:(mixer.parameterRevision, mixer.busEq(4,root.selectedBand));onChanged:function(v){mixer.setBusEq(4,root.selectedBand,v)};Layout.fillWidth:true}
                            KnobSlider {label:"EQ FREQ";from:20;to:20000;decimals:0;value:(mixer.parameterRevision, mixer.busEqFreq(4,root.selectedBand));onChanged:function(v){mixer.setBusEqFreq(4,root.selectedBand,v)};Layout.fillWidth:true}
                            KnobSlider {label:"EQ Q";from:.2;to:12;resetValue:1;value:(mixer.parameterRevision, mixer.busEqQ(4,root.selectedBand));onChanged:function(v){mixer.setBusEqQ(4,root.selectedBand,v)};Layout.fillWidth:true}
                        }
                    }
                    Rack {Layout.fillWidth:true;Layout.fillHeight:true
                        ColumnLayout {anchors.fill:parent;anchors.margins:8;spacing:4
                            RowLayout {Layout.fillWidth:true
                            Text {text:"MAIN GR "+mixer.mainGR.toFixed(1)+" dB · OUTPUT LIMIT "+mixer.limiterGR.toFixed(1)+" dB";color:"#ffbd65";font.pixelSize:11}
                            Item {Layout.fillWidth:true}
                            Btn {text:root.busDynOn(4,root.busDynUnit)?"● ON":"○ BYPASS";active:root.busDynOn(4,root.busDynUnit);onClicked:mixer.setBusDynamicsEnabled(4,root.busDynUnit,!root.busDynOn(4,root.busDynUnit))}
                            Repeater {model:["G","C","L"];Btn {text:modelData;active:root.busDynUnit===index;onClicked:root.busDynUnit=index}}
                            }
                            DynamicsGraph {isBus:true;channel:4;unit:root.busDynUnit;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:120}
                            Repeater {model:["threshold","ratio","attack","release"]
                                KnobSlider {property string key:modelData;label:key.toUpperCase();Layout.fillWidth:true
                                    from:key==="threshold"?-60:key==="ratio"?1:key==="attack"?1:10
                                    to:key==="threshold"?0:key==="ratio"?20:key==="attack"?200:1000
                                    resetValue:key==="threshold"?-18:key==="ratio"?2:key==="attack"?10:120
                                    value:(mixer.parameterRevision, mixer.busDynamics(4,root.busDynUnit,key))
                                    onChanged:function(v){mixer.setBusDynamics(4,root.busDynUnit,key,v)}
                                }
                            }
                        }
                    }
                }
            }}
            // 11 SHOW: import/export of QSettings .conf, ALSA device selection stays untouched.
            Item {ColumnLayout {anchors.fill:parent;anchors.margins:16;spacing:12
                Text {text:"SHOW · SCENE E PRESET";color:root.cyan;font.pixelSize:20;font.bold:true}
                Text {text:"Salva o carica tutti i parametri del mixer da file .conf. Il dispositivo ALSA rimane invariato.";color:"#c2dce9";wrapMode:Text.Wrap;Layout.fillWidth:true}
                Rack {Layout.fillWidth:true;Layout.preferredHeight:205
                    ColumnLayout {anchors.fill:parent;anchors.margins:15;spacing:10
                        Text {text:"FILE SHOW (.conf)";color:"#ffbd65";font.bold:true}
                        TextField {id:showFile;Layout.fillWidth:true;text:"show_01.conf";placeholderText:"show_01.conf oppure /percorso/file.conf";selectByMouse:true}
                        RowLayout {Layout.fillWidth:true;spacing:12
                            Btn {text:"SALVA SHOW";Layout.fillWidth:true;onClicked:mixer.exportShow(showFile.text)}
                            Btn {text:"CARICA SHOW";Layout.fillWidth:true;onClicked:confirmShow.open()}
                            Btn {text:"RESET";Layout.fillWidth:true;onClicked:confirmReset.open()}
                        }
                        Text {text:mixer.showMessage;Layout.fillWidth:true;wrapMode:Text.Wrap;color:root.cyan;font.pixelSize:12}
                    }
                }
                Text {text:"Cartella predefinita: ~/storm_shows/ · File impostazioni Qt: "+mixer.configFile();color:"#91b4c9";wrapMode:Text.Wrap;Layout.fillWidth:true}
                Text {text:"ATTENZIONE: il caricamento modifica immediatamente fader, routing e DSP. Abbassa il MAIN prima di caricare uno SHOW.";color:"#ffbd65";wrapMode:Text.Wrap;Layout.fillWidth:true}
                Item {Layout.fillHeight:true}
                Dialog {id:confirmReset;title:"Resettare il mixer?";modal:true;standardButtons:Dialog.Ok | Dialog.Cancel
                    onAccepted:mixer.resetShow()
                    Label {text:"Tutti i parametri DSP e routing saranno azzerati."}
                }
                Dialog {id:confirmShow;title:"Caricare lo SHOW?";modal:true;standardButtons:Dialog.Ok | Dialog.Cancel
                    onAccepted:mixer.importShow(showFile.text)
                    Label {text:"I parametri audio verranno applicati subito. Continua?"}
                }
            }}
        }
        Rectangle {Layout.fillWidth:true; Layout.preferredHeight:26; color:"#0a1b29"; border.color:root.borderCol
            RowLayout {anchors.fill:parent; anchors.leftMargin:12; anchors.rightMargin:12
                Text {text:"STORM MIX Native 0.6.22 · "+root.tabs[root.page]; color:"#a4c4d7"; font.pixelSize:11}
                Item {Layout.fillWidth:true}
                Text {text:"PROTOTIPO AUDIO · NON UTILIZZARE LIVE"; color:"#ffbe72"; font.pixelSize:10}
            }
        }
    }
}
