return {id="shiny.postprocess",modules={"postprocessing"},
    display={width=480,height=270,scale="integer"},
    resources={keeper={type="image",path="assets/keeper.png"},
        bloom_x={type="shader",path="shaders/bloom_x.frag"},
        bloom_y={type="shader",path="shaders/bloom_y.frag"},
        bloom_mix={type="shader",path="shaders/bloom_mix.frag"},
        grade={type="shader",path="shaders/grade.frag"},
        warp={type="shader",path="shaders/warp.frag"}}}
