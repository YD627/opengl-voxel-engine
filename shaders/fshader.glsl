#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;
in vec4 FragPosLightSpace;

uniform sampler2D texture1;
uniform sampler2D shadowMap;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 skyColor;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0) return 0.0;
    
    float closestDepth = texture(shadowMap, projCoords.xy).r; 
    float currentDepth = projCoords.z;
    
    // [修复] 减小偏移量，让影子紧贴物体底部
    // 原来是 0.005，现在改为 0.001
    float bias = max(0.001 * (1.0 - dot(normal, lightDir)), 0.0001);
    
    // PCF 柔化
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
    
    return shadow;
}

void main()
{
    vec4 texColor = texture(texture1, TexCoord);
    if(texColor.a < 0.1) discard;

    // 稍微调亮一点环境光，让阴影里的东西不那么黑
    vec3 ambient = 0.5 * texColor.rgb;
    
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * texColor.rgb;
    
    float shadow = ShadowCalculation(FragPosLightSpace, norm, lightDir);       
    vec3 lighting = (ambient + (1.0 - shadow) * diffuse);    
    
    float distance = length(viewPos - FragPos);
    float fogFactor = clamp((distance - 10.0) / (45.0 - 10.0), 0.0, 1.0);
    vec3 finalColor = mix(lighting, skyColor, fogFactor);

    FragColor = vec4(finalColor, texColor.a);
}