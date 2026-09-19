#version 330 core
in vec4 vColor;
in vec3 vNormal;

out vec4 FragColor;

// Per-direction brightness, the way the game shades blocks: top brightest, bottom darkest, and
// the two horizontal directions in between. Every factor is <= 1.0, which is what keeps bright
// blocks (sand, snow, quartz) from being clipped to white the way an additive light rig does.
// The top stays at 1.0 -- it is the face that carries the block's actual color, so raising it
// would only clip -- and the others are lifted towards it to keep the whole model from reading
// as too dark.
float faceShade(vec3 n)
{
    n = normalize(n);
    if (abs(n.y) >= abs(n.x) && abs(n.y) >= abs(n.z)) return n.y > 0.0 ? 1.00 : 0.56;
    if (abs(n.x) >= abs(n.z)) return 0.71;
    return 0.86;
}

void main()
{
    if (vColor.a <= 0.0) discard;

    // The shade multiplies the sRGB value directly, the way the game does it. Doing the
    // multiply in linear space instead (pow 2.2 / pow 1/2.2) would encode the factors back to
    // 0.90 / 0.80 / 0.73 on screen, which flattens the faces into each other; the whole point of
    // a per-face shade is the contrast between them.
    FragColor = vec4(vColor.rgb * faceShade(vNormal), vColor.a);
}