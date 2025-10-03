#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// Vertex Shader 소스 코드
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
}
)";

// Fragment Shader 소스 코드
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

void main()
{
    FragColor = vec4(1.0, 0.0, 0.0, 1.0); // 빨간색
}
)";

int main() {
    // GLFW 초기화
    if (!glfwInit()) {
        std::cout << "GLFW 초기화 실패!" << std::endl;
        return -1;
    }

    // OpenGL 버전 설정
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 윈도우 생성
    GLFWwindow* window = glfwCreateWindow(800, 600, "My Game Engine - Triangle", NULL, NULL);
    if (!window) {
        std::cout << "윈도우 생성 실패!" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    // GLAD 초기화
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "GLAD 초기화 실패!" << std::endl;
        return -1;
    }

    std::cout << "OpenGL 버전: " << glGetString(GL_VERSION) << std::endl;

    // ===== Vertex Shader 컴파일 =====
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // 컴파일 에러 체크
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "Vertex Shader 컴파일 실패:\n" << infoLog << std::endl;
    }

    // ===== Fragment Shader 컴파일 =====
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "Fragment Shader 컴파일 실패:\n" << infoLog << std::endl;
    }

    // ===== Shader Program 링크 =====
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "Shader Program 링크 실패:\n" << infoLog << std::endl;
    }

    // 셰이더 삭제 (이미 프로그램에 링크됨)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // ===== 삼각형 정점 데이터 =====
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  // 왼쪽 아래
         0.5f, -0.5f, 0.0f,  // 오른쪽 아래
         0.0f,  0.5f, 0.0f   // 위쪽 중앙
    };

    // VAO, VBO 생성
    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    // VAO 바인드
    glBindVertexArray(VAO);

    // VBO에 정점 데이터 복사
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 정점 속성 설정
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 바인드 해제
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    std::cout << "삼각형 렌더링 준비 완료!" << std::endl;

    // ===== 게임 루프 =====
    while (!glfwWindowShouldClose(window)) {
        // 입력 처리
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // 화면 클리어 (어두운 배경)
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 셰이더 프로그램 사용
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        // 삼각형 그리기!
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 버퍼 교체 및 이벤트 처리
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 정리
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}