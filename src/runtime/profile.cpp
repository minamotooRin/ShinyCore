#include "profile.h"
#include "shiny/script.h"
#include "shiny/projectiles.h"
#include "../platform/process_memory.h"
#include <algorithm>
#include <iomanip>
#include <ostream>

ScProfileWriter::ScProfileWriter(std::ostream& out,bool graphics,bool gpu_supported,bool diagnostics,const char* renderer)
    :output_(out),gpu_supported_(gpu_supported) {
    out<<std::setprecision(9)<<"{\"type\":\"header\",\"version\":1,\"engine\":\""<<SC_VERSION
       <<"\",\"graphics\":"<<(graphics?"true":"false")<<",\"gpu_timer\":\""
       <<(gpu_supported?"gl_time_elapsed":graphics?"unsupported":"headless")
       <<"\",\"trace_or_record\":"<<(diagnostics?"true":"false")
       <<",\"renderer\":"<<(renderer?sc_json_write(ScValue{std::string(renderer)}):"null")<<"}\n";
}
void ScProfileWriter::frame(ScFrameProfile& frame,const ScScript& script,std::uint64_t ticks) {
    frame.diagnostic_ms+=frame.render.capture_ms;
    const double wall=sc_profile_ms(frame.start);
    const double elapsed=std::chrono::duration<double,std::milli>(frame.start-start_).count();
    const double wait=frame.pace_ms+frame.render.present_ms;
    const auto& world=*script.world;
    const auto memory=sc_process_memory();
    auto optional=[&](const auto& value) { if(value) output_<<*value; else output_<<"null"; };
    output_<<"{\"type\":\"frame\",\"frame\":"<<frames_++<<",\"tick\":"<<ticks
        <<",\"elapsed_ms\":"<<elapsed<<",\"wall_ms\":"<<wall
        <<",\"cpu_ms\":"<<std::max(0.0,wall-wait-frame.diagnostic_ms)
        <<",\"wait_ms\":"<<wait<<",\"diagnostic_ms\":"<<frame.diagnostic_ms<<",\"steps\":"<<frame.steps
        <<",\"phases\":{\"script_update_ms\":"<<frame.script_update_ms<<",\"script_draw_ms\":"<<frame.script_draw_ms
        <<",\"simulation_ms\":"<<frame.simulation_ms<<",\"physics_ms\":"<<frame.step.physics_ms
        <<",\"entities_ms\":"<<frame.step.entities_ms<<",\"projectiles_ms\":"<<frame.step.projectiles_ms
        <<",\"particles_ms\":"<<frame.step.particles_ms<<",\"audio_ms\":"<<frame.audio_ms
        <<",\"room_ms\":"<<frame.room_ms<<",\"render_submit_ms\":"<<frame.render.submit_ms
        <<",\"input_poll_ms\":"<<frame.render.poll_ms<<"},\"entities\":"
        <<std::count_if(world.entities.begin(),world.entities.end(),[](const auto& e){return e.alive;})
        <<",\"entity_capacity\":"<<world.entities.size()<<",\"particles\":"
        <<world.particles.count
        <<",\"particle_capacity\":"<<world.particles.capacity()
        <<",\"projectiles\":"<<(world.projectiles?world.projectiles->count:0)
        <<",\"projectile_capacity\":"<<(world.projectiles?world.projectiles->x.size():0)
        <<",\"projectile_hits_total\":"<<(world.projectiles?world.projectiles->total_hits:0)
        <<",\"draw_commands\":"<<world.draw_count<<",\"draw_capacity\":"<<world.draws.size()
        <<",\"lua_bytes\":"<<script.memory_used<<",\"resident_bytes\":";
    optional(memory.resident); output_<<",\"peak_resident_bytes\":"; optional(memory.peak_resident);
    // Do not pass queue lengths or an estimated budget off as GPU draw calls/VRAM.
    output_<<",\"gpu_draw_calls\":null,\"gpu_resource_bytes\":null,\"native_allocations\":null,\"gpu_issued\":"
        <<(frame.render.gpu_issued?"true":"false")<<"}\n";
    gpu_issued_+=frame.render.gpu_issued;
    for(std::size_t i=0;i<frame.render.gpu_count;++i) {
        const auto& sample=frame.render.gpu[i];
        output_<<"{\"type\":\"gpu\",\"frame\":"<<sample.frame<<",\"gpu_ms\":"<<sample.ms<<"}\n";
        ++gpu_received_;
    }
}
void ScProfileWriter::finish() {
    output_<<"{\"type\":\"end\",\"frames\":"<<frames_<<",\"gpu_samples\":"<<gpu_received_
        <<",\"gpu_pending\":"<<gpu_issued_-gpu_received_<<",\"gpu_skipped\":"
        <<(gpu_supported_?frames_-gpu_issued_:0)<<"}\n";
    output_.flush();
}
