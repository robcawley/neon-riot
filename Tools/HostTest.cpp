#include <juce_audio_utils/juce_audio_utils.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <set>
#include <random>
#include <numeric>

using namespace juce;
namespace {
constexpr double rate = 48000;
constexpr int blockSize = 64;
int failures = 0;
void require (bool ok, const String& what) {
    if (! ok) { ++failures; std::printf ("FAIL %s\n", what.toRawUTF8()); }
}
struct Host {
    AudioPluginFormatManager formats;
    PluginDescription description;
    bool verifiedPath=false;
    Host() {
        formats.addFormat (new AudioUnitPluginFormat());
        OwnedArray<PluginDescription> found;
        formats.getFormat (0)->findAllTypesForFile (found, "AudioUnit:Synths/aumu,NRio,Rcwl");
        if (found.isEmpty()) throw std::runtime_error ("installed AU not found");
        description = *found[0];
        std::printf ("Installed AU: %s; %s\n", description.name.toRawUTF8(), description.fileOrIdentifier.toRawUTF8());
        require (File::getSpecialLocation (File::userHomeDirectory).getChildFile (
            "Library/Audio/Plug-Ins/Components/NEON RIOT.component").isDirectory(), "installed component path");
    }
    std::unique_ptr<AudioPluginInstance> make() {
        String error;
        auto p = formats.createPluginInstance (description, rate, blockSize, error);
        if (! p) throw std::runtime_error (error.toStdString());
        if(!verifiedPath){
            const auto expected=File::getSpecialLocation(File::userHomeDirectory).getChildFile(
                "Library/Audio/Plug-Ins/Components/NEON RIOT.component/Contents/MacOS/NEON RIOT").getFullPathName();
            bool loaded=false;for(uint32_t i=0;i<_dyld_image_count();++i)if(String(_dyld_get_image_name(i))==expected)loaded=true;
            require(loaded,"loaded image is the installed user component");
            std::printf("AU image: %s\n",expected.toRawUTF8());verifiedPath=true;
        }
        p->setPlayConfigDetails (0, 2, rate, blockSize);
        return p;
    }
};
AudioProcessorParameter* named (AudioProcessor& p, const String& name) {
    for (auto* v : p.getParameters()) if (v->getName (128) == name) return v;
    throw std::runtime_error (("unknown parameter " + name).toStdString());
}
void text (AudioProcessor& p, const String& name, const String& value) {
    auto* v = named (p, name); v->setValueNotifyingHost (v->getValueForText (value));
}
void norm (AudioProcessor& p, const String& name, float value) { named (p, name)->setValueNotifyingHost (value); }
// JUCE's stock integer parameters are exposed by this AU as generic 0..1 controls.
// Preserve those existing AU ranges; enumerate their stored integer steps explicitly.
int discreteSteps (AudioProcessorParameter& p) {
    const auto name=p.getName(128);
    if(name=="Pitch Bend Range")return 24;
    if(name.endsWith(" Octave")&&name.startsWith("Osc "))return 7;
    if(name.endsWith(" Semitone")&&name.startsWith("Osc "))return 25;
    return p.isDiscrete()?p.getNumSteps():0;
}
struct Meter {
    double peak = 0, square = 0, jumpSquare = 0, maxJump = 0;
    float previous[2] {};
    uint64_t samples = 0, bad = 0;
    std::vector<double> jumps;
    size_t maxIndex = 0;
    void add (const AudioBuffer<float>& b) {
        for (int c = 0; c < b.getNumChannels(); ++c) for (int n = 0; n < b.getNumSamples(); ++n) {
            const float x = b.getSample (c, n);
            if (! std::isfinite (x)) { ++bad; continue; }
            peak = std::max (peak, (double) std::abs (x)); square += x*x;
            const double d = x - previous[c]; previous[c] = x;
            jumpSquare += d*d; if (std::abs(d)>maxJump) {maxJump=std::abs(d);maxIndex=jumps.size();}
            jumps.push_back(d); ++samples;
        }
    }
    double rms() const { return std::sqrt (square / std::max (uint64_t (1), samples)); }
    double ratio() const { return maxJump / std::max (1e-12, std::sqrt (jumpSquare / std::max (uint64_t (1), samples))); }
    double localRatio() const {
        const size_t start=maxIndex>128?maxIndex-128:0,end=std::min(jumps.size(),maxIndex+129);
        double power=0;for(size_t k=start;k<end;++k)power+=jumps[k]*jumps[k];
        return maxJump/std::max(1e-12,std::sqrt(power/std::max(size_t(1),end-start)));
    }
    bool safe() const { return bad == 0 && peak <= 1.000001; }
};
void notes (MidiBuffer& m, bool on) {
    for (int key : {36, 43}) m.addEvent (on ? MidiMessage::noteOn (1, key, (uint8) 100) : MidiMessage::noteOff (1, key), 0);
}
void base (AudioPluginInstance& p, bool active) {
    p.releaseResources(); p.setCurrentProgram (0);
    // Neutral macros, audible sustain and a short release for recovery tests.
    for (const char* n : {"WEIGHT", "BITE", "ACID", "DRIVE", "CHAOS", "MOTION", "SPACE", "OUTPUT"}) {
        for (auto* v : p.getParameters()) if (v->getName (128).equalsIgnoreCase (String ("Macro ") + n)) v->setValueNotifyingHost (.5f);
    }
    text (p, "Amp Env Sustain", "80%"); text (p, "Amp Env Attack", "5ms"); text (p, "Amp Env Release", "80ms");
    text (p, "Output", "-12dB");
    if (active) {
        text (p, "Osc 2 Level", "50%"); text (p, "Noise Level", "15%");
        text (p, "Bite Amount", "40%");
        text (p, "Chorus Mix", "25%"); text (p, "Shift Mix", "20%"); text (p, "Shift Frequency", "3Hz");
        text (p, "Delay Mix", "20%"); text (p, "Delay Time", "30ms"); norm (p, "Delay Tempo Sync", 0);
        text (p, "Reverb Mix", "20%"); text (p, "CHAOS Intensity", "30%"); norm (p, "CHAOS Select", 6.0f/7.0f);
        norm (p, "LFO 1 Destination", 2.0f/9.0f); norm (p, "LFO 2 Destination", 3.0f/9.0f);
        text (p, "LFO 1 Amount", "30%"); text (p, "LFO 2 Amount", "20%");
    }
    p.prepareToPlay (rate, blockSize);
}
void inventory (AudioPluginInstance& p) {
    std::set<String> names, ids;
    std::printf ("\n| Index | Name | AU ID | Automatable | Discrete | Bool | Steps | Default |\n|---|---|---|---|---|---|---|---|\n");
    for (auto* v : p.getParameters()) {
        const String name = v->getName (128);
        const auto id = dynamic_cast<HostedAudioProcessorParameter*> (v)->getParameterID();
        require (name.isNotEmpty() && names.insert (name).second && ids.insert (id).second && v->isAutomatable(), "inventory " + name);
        std::printf ("| %d | %s | %s | %d | %d | %d | %d | %s |\n", v->getParameterIndex(), name.toRawUTF8(), id.toRawUTF8(),
            v->isAutomatable(), v->isDiscrete(), v->isBoolean(), v->getNumSteps(), v->getText (v->getDefaultValue(), 256).toRawUTF8());
    }
    require (p.getParameters().size() == 110, "110 parameters");
}
std::vector<float> probe (AudioPluginInstance& p, bool active, int index, float value, String& readback, bool gated=false) {
    base (p, active);
    if(gated){
        norm(p,"Drive Engine",1);text(p,"Pre Drive","70%");text(p,"Post Drive","60%");
        text(p,"Sub Anchor","70%");norm(p,"CHAOS Mode",1);text(p,"CHAOS Morph","5.4");
        norm(p,"Delay Tempo Sync",1);text(p,"LFO 1 Rate","3Hz");text(p,"LFO 2 Rate","2Hz");
        if(p.getParameters()[index]->getName(128).endsWith("Division")){
            norm(p,"LFO 1 Tempo Sync",1);norm(p,"LFO 2 Tempo Sync",1);
        }
    }
    AudioBuffer<float> b (2, blockSize); MidiBuffer m;
    for (int k = 0; k < 188; ++k) { m.clear(); if (k == 0) {notes (m, true);if(gated){m.addEvent(MidiMessage::pitchWheel(1,12000),0);m.addEvent(MidiMessage::controllerEvent(1,1,96),0);}} p.processBlock (b,m); }
    if(gated&&p.getParameters()[index]->getName(128).startsWith("HOLD ")){
        norm(p,"CHAOS HOLD",1);m.clear();p.processBlock(b,m);
    }
    auto* v = p.getParameters()[index]; v->setValueNotifyingHost (value);
    const int steps = discreteSteps(*v);
    const float tolerance = steps > 0 && value > 0 && value < 1 ? 0.5f / (float) (steps-1) + .0001f : .0001f;
    require (std::abs (v->getValue() - value) <= tolerance, "host write " + v->getName (128));
    readback = v->getText (v->getValue(), 256);
    std::vector<float> result;Meter safety;
    for (int k = 0; k < 75; ++k) {
        m.clear();
        if (k == 40) m.addEvent (MidiMessage::noteOn (1, 48, (uint8) 105), 0);
        if (k == 60) m.addEvent (MidiMessage::allNotesOff (1), 0);
        p.processBlock (b,m);safety.add(b);
        for (int c=0;c<2;++c) for (int n=0;n<blockSize;++n) result.push_back (b.getSample(c,n));
    }
    require(safety.safe(),"host write audio safety "+v->getName(128));
    return result;
}
double difference (const std::vector<float>& a, const std::vector<float>& b) {
    double result=0; for (size_t k=0;k<a.size();++k) result=std::max(result,(double)std::abs(a[k]-b[k])); return result;
}
void programs(AudioPluginInstance& p){
    require(p.getNumPrograms()==56,"installed AU exposes 56 factory programs");
    require(p.getProgramName(0)=="Riot Init"&&p.getProgramName(35)=="Meltdown Rail","original bank boundaries unchanged");
    std::set<String> names;
    for(int i=0;i<p.getNumPrograms();++i){const auto name=p.getProgramName(i);
        require(name.isNotEmpty()&&names.insert(name).second,"factory program name "+String(i));
        if(i>=36){p.setCurrentProgram(i);require(p.getCurrentProgram()==i,"host selects cinematic program "+String(i));}
        std::printf("PROGRAM %02d %s\n",i,name.toRawUTF8());
    }
}
void writes (AudioPluginInstance& p) {
    for(bool active:{false,true}){String readback;const auto a=probe(p,active,0,0,readback),b=probe(p,active,0,0,readback);
        require(difference(a,b)==0,"deterministic fixed probe");}

    std::printf ("\n| Parameter | Base max difference | Active max difference | Readback at 0 / .5 / 1 |\n|---|---|---|---|\n");
    for (int i=0;i<p.getParameters().size();++i) {
        double changes[2] {}; StringArray readbacks;
        for (int patch=0;patch<2;++patch) {
            String s; const auto a=probe(p,patch!=0,i,0,s); if(patch==1)readbacks.add(s);
            for(float n:{.5f,1.0f}) { auto b=probe(p,patch!=0,i,n,s); changes[patch]=std::max(changes[patch],difference(a,b)); if(patch==1)readbacks.add(s); }
        }
        std::printf("| %s | %.7g | %.7g | %s |\n",p.getParameters()[i]->getName(128).toRawUTF8(),changes[0],changes[1],readbacks.joinIntoString(" / ").toRawUTF8());
    }
}
void gatedWrites(AudioPluginInstance& p){
    std::printf("\n| Dependency probe | Max audio difference |\n|---|---|\n");
    for(const char* name:{"Pitch Bend Range","HOLD Size","HOLD Pitch","CHAOS Morph","Delay Division","Drive Tilt",
        "Pre Bias","Post Bias","Sub Anchor Freq","LFO 1 Division","LFO 2 Division","LFO 1 Tempo Sync","LFO 2 Tempo Sync","Mod Wheel to Cutoff"}){
        const int index=named(p,name)->getParameterIndex();String readback;
        const auto a=probe(p,true,index,0,readback,true);double changed=0;
        for(float v:{.5f,1.0f})changed=std::max(changed,difference(a,probe(p,true,index,v,readback,true)));
        require(changed>1e-7,String("dependency probe audible: ")+name);
        std::printf("| %s | %.8g |\n",name,changed);
    }
}
void ramps (AudioPluginInstance& p) {
    std::printf("\n| Continuous parameter | Peak | Max jump | Jump / typical | Local ratio | Fixed-control max jump | Review |\n|---|---|---|---|---|---|---|\n");
    for(int i=0;i<p.getParameters().size();++i) {
        auto* v=p.getParameters()[i]; if(discreteSteps(*v)>0)continue;
        base(p,true); v->setValueNotifyingHost(0); Meter meter; AudioBuffer<float>b(2,blockSize); MidiBuffer m;
        for(int k=0;k<375;++k) { m.clear();if(k==0)notes(m,true);p.processBlock(b,m); }
        for(int c=0;c<2;++c)meter.previous[c]=b.getSample(c,blockSize-1);
        for(int k=0;k<1500;++k) {
            const float n=k<750?(float)k/749.0f:(1499.0f-k)/749.0f; v->setValueNotifyingHost(n);
            m.clear();p.processBlock(b,m);meter.add(b);
        }
        require(meter.safe(),"ramp safety "+v->getName(128));
        base(p,true);v->setValueNotifyingHost(0);Meter control;
        for(int k=0;k<1875;++k){m.clear();if(k==0)notes(m,true);p.processBlock(b,m);
            if(k==374)for(int c=0;c<2;++c)control.previous[c]=b.getSample(c,blockSize-1);
            if(k>=375)control.add(b);}
        const bool flag=meter.ratio()>12&&meter.localRatio()>12&&meter.maxJump>2*control.maxJump;
        std::printf("| %s | %.6f | %.6f | %.2f | %.2f | %.6f | %s |\n",v->getName(128).toRawUTF8(),meter.peak,meter.maxJump,meter.ratio(),meter.localRatio(),control.maxJump,flag?"REVIEW":"within threshold");
    }
}
void discrete (AudioPluginInstance& p) {
    std::printf("\n| Discrete parameter | Steps | Peak | Max jump | Jump / typical | Recovery RMS | Released RMS |\n|---|---|---|---|---|---|---|\n");
    for(int i=0;i<p.getParameters().size();++i) {
        auto* v=p.getParameters()[i];if(discreteSteps(*v)==0)continue;
        base(p,false);const float original=v->getValue();Meter meter;AudioBuffer<float>b(2,blockSize);MidiBuffer m;
        for(int k=0;k<375;++k){m.clear();if(k==0)notes(m,true);p.processBlock(b,m);}
        for(int c=0;c<2;++c)meter.previous[c]=b.getSample(c,blockSize-1);
        const int steps=discreteSteps(*v);require(steps>1&&steps<100,"discrete step count");
        for(int step=0;step<steps;++step){v->setValueNotifyingHost((float)step/(steps-1));for(int k=0;k<150;++k){m.clear();p.processBlock(b,m);meter.add(b);}}
        // Clear latching performance controls, restore the probe, and prove fresh notes and note-offs work.
        v->setValueNotifyingHost(original);norm(p,"CHAOS HOLD",0);norm(p,"CHAOS PUNCH",0);Meter recovery,release;
        for(int k=0;k<750;++k){m.clear();if(k==0)notes(m,true);p.processBlock(b,m);if(k>=675)recovery.add(b);}
        for(int k=0;k<7500;++k){m.clear();if(k==0)notes(m,false);p.processBlock(b,m);if(k>=7425)release.add(b);}
        require(meter.safe()&&recovery.rms()>1e-6&&release.rms()<.000031623,"discrete safety/recovery "+v->getName(128));
        std::printf("| %s | %d | %.6f | %.6f | %.2f | %.7g | %.7g |\n",v->getName(128).toRawUTF8(),steps,meter.peak,meter.maxJump,meter.ratio(),recovery.rms(),release.rms());
    }
}

void transitions(AudioPluginInstance& p){
    for(const char* name:{"Voice Mode","Drive Engine"}){
        base(p,false);text(p,"Pre Drive","80%");text(p,"Post Drive","70%");text(p,"Cutoff","5k");
        norm(p,"Pre Drive Type",.75f);norm(p,"Post Drive Type",.75f);
        AudioBuffer<float>b(2,blockSize);MidiBuffer m;Meter meter;
        for(int k=0;k<750;++k){m.clear();if(k==0)notes(m,true);p.processBlock(b,m);}
        for(int c=0;c<2;++c)meter.previous[c]=b.getSample(c,blockSize-1);
        norm(p,name,1);
        for(int k=0;k<150;++k){m.clear();p.processBlock(b,m);meter.add(b);}
        std::printf("TRANSITION %s: peak %.6f max jump %.6f jump/typical %.2f last-block RMS %.8g\n",name,meter.peak,meter.maxJump,meter.ratio(),b.getRMSLevel(0,0,blockSize));
        require(meter.safe(),String("transition safety ")+name);
    }
    base(p,true);AudioBuffer<float>b(2,blockSize);MidiBuffer m;norm(p,"CHAOS PUNCH",1);
    for(int k=0;k<750;++k){m.clear();if(k==0)notes(m,true);p.processBlock(b,m);}
    require(named(p,"CHAOS PUNCH")->getValue()==1,"PUNCH stays held through processing");
    norm(p,"CHAOS PUNCH",0);m.clear();p.processBlock(b,m);
    require(named(p,"CHAOS PUNCH")->getValue()==0,"PUNCH releases on host write");
    std::printf("PUNCH host-held 1 second then released: PASS\n");
}
// AU class-info is a property list containing the plug-in's normal binary state under "jucePluginState".
MemoryBlock mapState (const MemoryBlock& input, bool add) {
    auto data=CFDataCreate(nullptr,(const UInt8*)input.getData(),(CFIndex)input.getSize());
    auto plist=CFPropertyListCreateWithData(nullptr,data,kCFPropertyListMutableContainersAndLeaves,nullptr,nullptr);CFRelease(data);
    if(!plist)throw std::runtime_error("AU state plist");
    auto dict=(CFMutableDictionaryRef)plist;
    auto payload=(CFDataRef)CFDictionaryGetValue(dict,CFSTR("jucePluginState"));
    if(!payload){CFRelease(plist);throw std::runtime_error("AU state data key");}
    auto xml=AudioProcessor::getXmlFromBinary(CFDataGetBytePtr(payload),(int)CFDataGetLength(payload));
    if(!xml){CFRelease(plist);throw std::runtime_error("AU embedded XML");}
    auto state=ValueTree::fromXml(*xml);
    if(add){auto map=state.getOrCreateChildWithName("MIDI_LEARN",nullptr);map.removeAllChildren(nullptr);
        const char* ids[]={"flt_cutoff","chaos_select","chaos_punch"};
        for(int k=0;k<3;++k){ValueTree e("MAP");e.setProperty("cc",21+k,nullptr);e.setProperty("param",ids[k],nullptr);map.appendChild(e,nullptr);}
    } else require(state.getChildWithName("MIDI_LEARN").getNumChildren()==3,"hosted MIDI map retained");
    MemoryBlock raw;AudioProcessor::copyXmlToBinary(*state.createXml(),raw);
    auto replacement=CFDataCreate(nullptr,(const UInt8*)raw.getData(),(CFIndex)raw.getSize());CFDictionarySetValue(dict,CFSTR("jucePluginState"),replacement);CFRelease(replacement);
    auto encoded=CFPropertyListCreateData(nullptr,plist,kCFPropertyListBinaryFormat_v1_0,0,nullptr);CFRelease(plist);
    MemoryBlock result(CFDataGetBytePtr(encoded),(size_t)CFDataGetLength(encoded));CFRelease(encoded);return result;
}
void stateTest (Host& host) {
    auto a=host.make(),b=host.make();std::mt19937 random(1201);std::uniform_real_distribution<float> value(0,1);
    for(auto* p:a->getParameters()){
        const int steps=discreteSteps(*p);
        p->setValueNotifyingHost(steps>0?(float)(random()%(unsigned)steps)/(steps-1):value(random));
    }
    std::vector<float> expected;for(auto* p:a->getParameters())expected.push_back(p->getValue());
    MemoryBlock state;a->getStateInformation(state);state=mapState(state,true);
    // Both are fresh instances restored from the identical saved state (including exact raw floats).
    a=host.make();a->setStateInformation(state.getData(),(int)state.getSize());b->setStateInformation(state.getData(),(int)state.getSize());
    for(int k=0;k<110;++k)require(a->getParameters()[k]->getValue()==b->getParameters()[k]->getValue()&&std::abs(a->getParameters()[k]->getValue()-expected[(size_t)k])<1e-6,"hosted state value "+String(k));
    MemoryBlock saved;b->getStateInformation(saved);mapState(saved,false);
    double maxDiff=0;
    for(double sr:{44100.0,48000.0,96000.0}){
        a->prepareToPlay(sr,blockSize);b->prepareToPlay(sr,blockSize);AudioBuffer<float>x(2,blockSize),y(2,blockSize);MidiBuffer ma,mb;
        for(int k=0;k<1500;++k){ma.clear();if(k==0)notes(ma,true);if(k==750)notes(ma,false);mb=ma;a->processBlock(x,ma);b->processBlock(y,mb);
            for(int c=0;c<2;++c)for(int n=0;n<blockSize;++n)maxDiff=std::max(maxDiff,(double)std::abs(x.getSample(c,n)-y.getSample(c,n)));}
    }
    for(int k=0;k<3;++k){AudioBuffer<float> audio(2,blockSize);MidiBuffer midi;
        midi.addEvent(MidiMessage::controllerEvent(1,21+k,127),0);b->processBlock(audio,midi);
        MessageManager::getInstance()->runDispatchLoopUntil(60);
        const char* names[]={"Cutoff","CHAOS Select","CHAOS PUNCH"};
        require(named(*b,names[k])->getValue()==1,String("restored MIDI mapping responds: ")+names[k]);}
    require(maxDiff==0,"hosted state identical audio");std::printf("Hosted state: 110 values, 3 MIDI mappings, 3 rates, maximum audio difference %.9g\n",maxDiff);
}
}
int main(int argc,char** argv){
    ScopedJuceInitialiser_GUI gui;std::setvbuf(stdout,nullptr,_IOLBF,0);
    try {Host host;auto p=host.make();const String mode=argc>1?argv[1]:"--all";
        if(!StringArray{"--all","--inventory","--writes","--ramps","--discrete","--state","--transitions","--gated","--programs"}.contains(mode)){
            std::fprintf(stderr,"Unknown host test mode: %s\n",mode.toRawUTF8());return 2;}
        if(mode=="--inventory"||mode=="--all")inventory(*p);
        if(mode=="--programs"||mode=="--all")programs(*p);
        if(mode=="--writes"||mode=="--all")writes(*p);
        if(mode=="--gated"||mode=="--all")gatedWrites(*p);
        if(mode=="--ramps"||mode=="--all")ramps(*p);
        if(mode=="--discrete"||mode=="--all")discrete(*p);
        if(mode=="--state"||mode=="--all")stateTest(host);
        if(mode=="--transitions"||mode=="--all")transitions(*p);
        std::printf("HOST TEST: %d failures\n",failures);return failures?1:0;
    }catch(const std::exception& e){std::fprintf(stderr,"HOST TEST ERROR: %s\n",e.what());return 2;}
}
