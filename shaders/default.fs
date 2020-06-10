#version 330 core

in Vertex {
	vec2 texCoord;
};

out vec4 fragColor;

uniform sampler2D source[];

void main() {
	fragColor = texture(source[0], texCoord);
}
