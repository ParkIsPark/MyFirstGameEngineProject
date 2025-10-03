#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// Vertex Shader �ҽ� �ڵ�
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
}
)";

// Fragment Shader �ҽ� �ڵ�
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

void main()
{
    FragColor = vec4(1.0, 0.0, 0.0, 1.0); // ������
}
)";

int main() {
    // GLFW �ʱ�ȭ
    if (!glfwInit()) {
        std::cout << "GLFW �ʱ�ȭ ����!" << std::endl;
        return -1;
    }

    // OpenGL ���� ����
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // ������ ����
    GLFWwindow* window = glfwCreateWindow(800, 600, "My Game Engine - Triangle", NULL, NULL);
    if (!window) {
        std::cout << "������ ���� ����!" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    // GLAD �ʱ�ȭ
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "GLAD �ʱ�ȭ ����!" << std::endl;
        return -1;
    }

    std::cout << "OpenGL ����: " << glGetString(GL_VERSION) << std::endl;

    // ===== Vertex Shader ������ =====
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // ������ ���� üũ
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "Vertex Shader ������ ����:\n" << infoLog << std::endl;
    }

    // ===== Fragment Shader ������ =====
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "Fragment Shader ������ ����:\n" << infoLog << std::endl;
    }

    // ===== Shader Program ��ũ =====
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "Shader Program ��ũ ����:\n" << infoLog << std::endl;
    }

    // ���̴� ���� (�̹� ���α׷��� ��ũ��)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // ===== �ﰢ�� ���� ������ =====
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  // ���� �Ʒ�
         0.5f, -0.5f, 0.0f,  // ������ �Ʒ�
         0.0f,  0.5f, 0.0f   // ���� �߾�
    };

    // VAO, VBO ����
    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    // VAO ���ε�
    glBindVertexArray(VAO);

    // VBO�� ���� ������ ����
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // ���� �Ӽ� ����
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // ���ε� ����
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    std::cout << "�ﰢ�� ������ �غ� �Ϸ�!" << std::endl;

    // ===== ���� ���� =====
    while (!glfwWindowShouldClose(window)) {
        // �Է� ó��
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // ȭ�� Ŭ���� (��ο� ���)
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ���̴� ���α׷� ���
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        // �ﰢ�� �׸���!
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ���� ��ü �� �̺�Ʈ ó��
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ����
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}