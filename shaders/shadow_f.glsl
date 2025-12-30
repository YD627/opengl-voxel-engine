#version 330 core
in vec2 TexCoord;
uniform sampler2D texture1;
uniform int hasTexture;

void main()
{
    if (hasTexture == 1) {
        float alpha = texture(texture1, TexCoord).a;
        if (alpha < 0.1) discard;
    }
    // 不输出颜色，只写深度
}