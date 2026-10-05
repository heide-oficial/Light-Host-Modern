#pragma once
// Included after GainPlugin; these checks use actual audio through the routing runtime.
class BusChangePlugin final : public GainPlugin
{
public:
    BusChangePlugin() : GainPlugin(BusesProperties()
        .withInput("Main", AudioChannelSet::stereo(), true).withInput("Optional", AudioChannelSet::stereo(), false)
        .withInput("Tail", AudioChannelSet::mono(), true)
        .withOutput("Main", AudioChannelSet::stereo(), true).withOutput("Optional", AudioChannelSet::stereo(), false)
        .withOutput("Tail", AudioChannelSet::mono(), true)) {}
};
static void verifyPluginBusChanges()
{
    using namespace lightHostModern;
    auto processor = std::make_unique<BusChangePlugin>();
    auto slot = std::make_shared<PluginSlot>(PluginDescription{}, std::move(processor));slot->instanceId="plugin";
    ChainSnapshot snapshot;snapshot.sampleRate=48000;snapshot.blockSize=64;snapshot.slots.push_back(slot);
    snapshot.graph=RoutingGraph::empty(2,2);RouteNode node;node.id="plugin";node.inputs=node.outputs=3;
    pluginBuses::synchronize(node,*slot->processor);snapshot.graph.nodes.push_back(node);
    snapshot.graph.edges={{"input","audio-in","plugin",0,2,1,1},{"output","plugin","audio-out",2,0,1,2}};
    auto& saved=*snapshot.graph.find("plugin");const auto tailKey=saved.inputPorts[2].key;
    const auto render=[&](float expected){
        slot->prepare(48000,64,jmax(slot->inputChannels,slot->outputChannels));
        const auto runtime=RoutingRuntime::compile(snapshot);AudioBuffer<float> audio(2,64);audio.clear();
        FloatVectorOperations::fill(audio.getWritePointer(0),.25f,64);std::atomic<uint64> failures{0};
        {realtimeAudit::Scope audit(realtimeAudit::Origin::host);runtime->process(audio,2,2,failures);}
        require(std::abs(audio.getSample(0,63)-expected)<.00001f&&std::abs(audio.getSample(1,63)-expected)<.00001f,"Saved channel routed to the wrong physical bus");
    };
    render(.5f);
    auto layout=slot->processor->getBusesLayout();layout.inputBuses.set(1,AudioChannelSet::stereo());layout.outputBuses.set(1,AudioChannelSet::stereo());
    slot->release();require(slot->processor->setBusesLayout(layout),"Cannot enable optional buses");
    require(!slot->layoutMatches(),"Dynamic layout change went undetected");slot->refreshLayout();pluginBuses::synchronize(saved,*slot->processor);
    require(saved.inputPorts[2].key==tailKey&&saved.inputPorts[2].physical==4,"Enabling a bus changed saved channel identity");
    require(saved.inputPorts[3].type==AudioChannelSet::left&&saved.inputPorts[4].type==AudioChannelSet::right,"Stereo types were not preserved");render(.5f);
    layout.inputBuses.set(2,AudioChannelSet::disabled());layout.outputBuses.set(2,AudioChannelSet::disabled());
    slot->release();require(slot->processor->setBusesLayout(layout),"Cannot disable buses");slot->refreshLayout();pluginBuses::synchronize(saved,*slot->processor);
    require(!RoutingGraph::available(saved,false,2)&&snapshot.graph.edges.size()==2,"Unavailable connections were removed");render(0);
    layout.inputBuses.set(2,AudioChannelSet::mono());layout.outputBuses.set(2,AudioChannelSet::mono());
    slot->release();require(slot->processor->setBusesLayout(layout),"Cannot restore buses");slot->refreshLayout();pluginBuses::synchronize(saved,*slot->processor);render(.5f);
    layout.inputBuses.set(0,AudioChannelSet::mono());layout.outputBuses.set(0,AudioChannelSet::mono());
    slot->release();require(slot->processor->setBusesLayout(layout),"Cannot select mono");slot->refreshLayout();pluginBuses::synchronize(saved,*slot->processor);
    require(!RoutingGraph::available(saved,false,0,2)&&saved.inputPorts.back().type==AudioChannelSet::centre,"Mono reconfiguration silently reused L/R connections");
    AudioProcessor::BusesLayout restored;require(pluginBuses::decode(pluginBuses::encode(layout),restored)&&restored==layout,"Bus formats did not round trip");
    RoutingGraph parsed;String error;require(RoutingGraph::parse(snapshot.graph.json(),parsed,error)&&parsed.find("plugin")->inputPorts[2].key==tailKey,"Saved port inventory did not round trip");
    // A legacy stereo session reopened with a mono plugin must not redirect L to centre.
    RouteNode legacy;legacy.inputs=legacy.outputs=2;legacy.inputNames={"Main L","Main R"};legacy.outputNames={"Main L","Main R"};
    pluginBuses::synchronize(legacy,*slot->processor);
    require(legacy.inputPorts[0].physical==-1&&legacy.inputPorts[1].physical==-1,"Legacy stereo wires were redirected into mono");
    GainPlugin discrete(2);RouteNode generic;generic.inputs=generic.outputs=2;pluginBuses::synchronize(generic,discrete);
    require(generic.inputPorts[0].type==AudioChannelSet::discreteChannel0&&generic.inputPorts[1].type!=AudioChannelSet::right,"Independent channels were mislabeled stereo");
    // Opposite-phase stereo cancels on downmix; mono is copied to both sides.
    for(bool downmix:{true,false}){
        ChainSnapshot s;s.sampleRate=48000;s.blockSize=64;s.graph=RoutingGraph::empty(2,2);
        s.graph.edges={{"wire","audio-in","audio-out",0,0,downmix?2:1,downmix?1:2}};
        auto runtime=RoutingRuntime::compile(s);AudioBuffer<float> audio(2,64);
        FloatVectorOperations::fill(audio.getWritePointer(0),.4f,64);FloatVectorOperations::fill(audio.getWritePointer(1),-.4f,64);std::atomic<uint64> failures{0};
        runtime->process(audio,2,2,failures);require(std::abs(audio.getSample(0,0)-(downmix?0.f:.4f))<.00001f,"Mono/stereo conversion changed");
        if(!downmix)require(std::abs(audio.getSample(1,0)-.4f)<.00001f,"Mono duplication failed");
    }
}
