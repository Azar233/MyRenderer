#version 330 core
in vec2 vUv;
uniform mat4 uInverseViewProjection;
uniform mat4 uViewProjection;
uniform vec3 uCamera;
out vec4 fragColor;

float grid(vec2 p, float spacing) {
    vec2 coord = p / spacing;
    vec2 width = max(fwidth(coord), vec2(0.00001));
    vec2 edge = abs(fract(coord - 0.5) - 0.5) / width;
    return (1.0 - min(min(edge.x, edge.y), 1.0))
        * (1.0 - smoothstep(0.25, 1.0, max(width.x, width.y)));
}

void main() {
    vec4 a = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, -1.0, 1.0);
    vec4 b = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 nearPoint = a.xyz / a.w;
    vec3 farPoint = b.xyz / b.w;
    vec3 ray = farPoint - nearPoint;
    if (abs(ray.y) < 0.00001) discard;
    float t = -nearPoint.y / ray.y;
    if (t <= 0.0 || t >= 1.0) discard;
    vec3 p = nearPoint + t * ray;
    vec4 clip = uViewProjection * vec4(p, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    float distanceFade = 1.0 - smoothstep(20.0, 85.0, length(p - uCamera));
    float minor = grid(p.xz, 0.25);
    float major = grid(p.xz, 1.0);
    float coarse = grid(p.xz, 10.0);
    float alpha = max(minor * 0.22, max(major * 0.42, coarse * 0.50)) * distanceFade;
    if (alpha < 0.002) discard;
    fragColor = vec4(vec3(0.38, 0.42, 0.49), alpha);
}
