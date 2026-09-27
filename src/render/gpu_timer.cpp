#include "gpu_timer.h"
#include "external/glad.h"

const char* sc_gpu_renderer() { return reinterpret_cast<const char*>(glGetString(GL_RENDERER)); }

bool ScGpuTimer::open() {
    close();
    if(!(GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_timer_query) || !glGetQueryObjectui64v) return false;
    GLint bits=0;
    glGetQueryiv(GL_TIME_ELAPSED,GL_QUERY_COUNTER_BITS,&bits);
    if(bits==0) return false;
    glGenQueries(static_cast<GLsizei>(queries_.size()),queries_.data());
    return queries_[0]!=0;
}
void ScGpuTimer::close() noexcept {
    if(active_) glEndQuery(GL_TIME_ELAPSED);
    if(queries_[0]) glDeleteQueries(static_cast<GLsizei>(queries_.size()),queries_.data());
    queries_.fill(0); read_=count_=0; active_=false;
}
void ScGpuTimer::begin(ScRenderProfile& out) {
    if(!queries_[0]) return;
    while(count_) {
        GLint available=0;
        glGetQueryObjectiv(queries_[read_],GL_QUERY_RESULT_AVAILABLE,&available);
        if(!available) break;
        GLuint64 ns=0;
        glGetQueryObjectui64v(queries_[read_],GL_QUERY_RESULT,&ns);
        out.gpu[out.gpu_count++]={frames_[read_],static_cast<double>(ns)/1e6};
        read_=(read_+1)%queries_.size(); --count_;
    }
    if(count_==queries_.size()) return;
    auto slot=(read_+count_)%queries_.size();
    frames_[slot]=out.frame;
    glBeginQuery(GL_TIME_ELAPSED,queries_[slot]);
    ++count_; active_=out.gpu_issued=true;
}
void ScGpuTimer::end() {
    if(active_) { glEndQuery(GL_TIME_ELAPSED); active_=false; }
}
