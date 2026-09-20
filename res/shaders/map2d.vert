#version 330 core

// Full-screen triangle generated from gl_VertexID: no vertex buffer is needed
// for a pass whose only job is to give the fragment stage one sample per pixel
// of the map viewport.
void main()
{
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
