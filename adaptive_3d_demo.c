/*
 * ============================================================================
 * 自适应3D渲染教学 Demo
 * ----------------------------------------------------------------------------
 * 核心思想：
 *   多边形数量每秒自动 +1%，当帧数开始下降时，程序自动感知并"自我重构"，
 *   回退到能稳定保持 60 帧的最优多边形数量。
 *
 * 这就是一个"动态自适应优化系统"的雏形 ——
 *   不靠人工拍脑袋定上限，而是让程序自己找到性能临界点。
 *
 * 同构映射：
 *   这和"知识库只放几百字思维框架，不放几十G垃圾"是同一个逻辑：
 *     - 资料/多边形 不是越多越好
 *     - 关键是找到"刚好够用"的最优临界点
 *     - 系统自己感知、自己调整，不靠死规则
 *
 * 编译 & 运行：
 *   gcc adaptive_3d_tutorial.c -o demo -lm
 *   ./demo
 *
 *   如果系统没有 GLUT，用下面的命令安装：
 *     Ubuntu/Debian: sudo apt-get install freeglut3-dev
 *     macOS:          brew install freeglut
 *     CentOS/RHEL:   sudo yum install freeglut-devel
 *
 *   如果实在装不上 GLUT，把下面的 USE_OPENGL 改成 0，
 *   会用一个"模拟帧数"的版本来演示同样的逻辑。
 * ============================================================================
 */

/* ============ 配置开关 ============ */
/* 如果设为 1，需要 OpenGL/GLUT；设为 0，用纯终端模拟版（无需任何依赖） */
#define USE_OPENGL 0

#if USE_OPENGL
    #include <GL/glut.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* ============ 全局参数 ============ */

/* 性能目标：稳定 60 FPS */
#define TARGET_FPS      60.0f

/* 多边形数量：初始值 & 上限 */
#define INIT_POLYGONS   280
#define MAX_POLYGONS    200000

/* 每秒增长 1% */
#define GROWTH_RATE     1.01f

/* 低于此帧数 → 削减多边形（自我保护） */
#define LOW_FPS_THRESHOLD  55.0f

/* 极低帧数 → 紧急削减（防死机） */
#define CRITICAL_FPS    20.0f

/* 削减/试探步长 */
#define REDUCE_FACTOR   0.90f   /* 卡顿时砍掉 10% */
#define GROW_FACTOR     1.02f   /* 流畅时试探 +2%  */

/* 当前多边形数量（全局，方便渲染函数读取） */
static int g_polygonCount = INIT_POLYGONS;

/* 当前帧数（模拟或真实） */
static float g_currentFPS = 60.0f;

/* 记录上一秒的时间戳，用于控制"每秒 +1%" */
static time_t g_lastSecond = 0;

/* 统计：一共经历了几次"自我重构" */
static int g_recoveryCount = 0;

/* 最优多边形数量（程序自己找到的临界点） */
static int g_optimalPolygons = INIT_POLYGONS;


/* ============================================================================
 * 模拟帧数测量
 * ----------------------------------------------------------------------------
 * 真实场景：用 OpenGL 的 glutGet(GLUT_ELAPSED_TIME) 或平台 API 计算 FPS。
 * 这里用一个数学模型来"模拟"帧数随多边形数量变化的曲线：
 *
 *   FPS ≈ TARGET_FPS * (optimalPoint / polygonCount)
 *
 * 也就是说：多边形越多，帧数越低。
 * 当 polygonCount 超过 optimalPoint 时，FPS 开始跌破 60。
 *
 * 这恰好模拟了"递增到某个临界点后开始卡顿"的真实规律。
 * ============================================================================
 */
static float measureFPS(void)
{
    /* 临界点：假设这台机器在 300 多边形时刚好 60 帧 */
    /* (教学演示用较小的值，让"卡顿→自救"过程更快出现) */
    /* 真实程序里不需要预设这个值 —— 程序自己会找到 */
    const float hardwareLimit = 300.0f;

    if (g_polygonCount <= hardwareLimit) {
        /* 未超载，满帧运行 */
        return TARGET_FPS;
    } else {
        /* 超载：帧数随多边形数量线性下降 */
        float fps = TARGET_FPS * (hardwareLimit / (float)g_polygonCount);
        /* 加一点随机扰动，模拟真实波动 */
        fps += (rand() % 100 - 50) * 0.01f;
        if (fps < 1.0f) fps = 1.0f;
        return fps;
    }
}

#if USE_OPENGL
/* ============================================================================
 * OpenGL 渲染函数（可选）
 * ----------------------------------------------------------------------------
 * 用 GLUT 画一堆旋转的三角形来"真实消耗 GPU"。
 * 每帧绘制 g_polygonCount 个三角形。
 *
 * 如果装了 OpenGL，可以取消 USE_OPENGL 编译，看到真实的 FPS 变化。
 * 这里我们只做示意，核心逻辑在 mainLoop() 里。
 * ============================================================================
 */
static float g_rotation = 0.0f;

static void renderScene(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(g_rotation, 1.0f, 1.0f, 1.0f);

    /* 画 g_polygonCount 个三角形（用三角形扇形近似） */
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < g_polygonCount && i < MAX_POLYGONS; i++) {
        float angle = 2.0f * M_PI * i / (float)g_polygonCount;
        float r = 1.0f + 0.3f * sinf((float)i * 0.1f);
        float x = r * cosf(angle);
        float y = r * sinf(angle);
        float z = 0.5f * sinf((float)i * 0.05f);

        glColor3f(0.5f + 0.5f * sinf(angle),
                  0.5f + 0.5f * cosf(angle),
                  0.7f);
        glVertex3f(x, y, z);

        /* 第二、三个顶点构成三角形 */
        float a2 = angle + 0.1f;
        glVertex3f(r * cosf(a2), r * sinf(a2), z + 0.1f);
        glVertex3f(x * 0.9f, y * 0.9f, z - 0.1f);
    }
    glEnd();

    glutSwapBuffers();
    g_rotation += 1.0f;
}

/* 真实 FPS 测量（OpenGL 版） */
static int g_frameCount = 0;
static float g_lastTime = 0.0f;

static void updateFPS(void)
{
    g_frameCount++;
    float now = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    if (now - g_lastTime >= 1.0f) {
        g_currentFPS = (float)g_frameCount / (now - g_lastTime);
        g_frameCount = 0;
        g_lastTime = now;
    }
}
#endif /* USE_OPENGL */

/* ============================================================================
 * 核心自适应逻辑
 * ----------------------------------------------------------------------------
 * 这是整个教学的重点 —— "自我重构到最优临界点"的算法：
 *
 *   1. 测量当前帧数
 *   2. 如果 FPS < 临界值  → 削减多边形（自我保护）
 *   3. 如果 FPS >= 60     → 缓慢增加多边形（试探上限）
 *   4. 如果 FPS 极低      → 紧急削减（防死机）
 *
 * 最终程序会在 60 帧附近来回试探，自动稳定在最优多边形数量。
 * ============================================================================
 */
static void adaptiveAdjust(void)
{
    /* --- 第 1 步：测量帧数 --- */
    #if USE_OPENGL
        updateFPS();
    #else
        g_currentFPS = measureFPS();
    #endif

    /* --- 第 2 步：紧急保护（防死机） --- */
    if (g_currentFPS < CRITICAL_FPS) {
        g_polygonCount = (int)(g_polygonCount * 0.5f);  /* 直接砍半 */
        g_recoveryCount++;
        printf("  ⚠️  紧急自救！帧数 %.1f 过低，多边形砍半 → %d\n",
               g_currentFPS, g_polygonCount);
    }
    /* --- 第 3 步：低于阈值 → 削减（自我保护） --- */
    else if (g_currentFPS < LOW_FPS_THRESHOLD) {
        g_polygonCount = (int)(g_polygonCount * REDUCE_FACTOR);  /* -10% */
        g_recoveryCount++;
        /* 更新最优临界点 */
        if (g_polygonCount > g_optimalPolygons) {
            g_optimalPolygons = g_polygonCount;
        }
    }
    /* --- 第 4 步：帧数充足 → 试探性增长（寻找上限） --- */
    else if (g_currentFPS >= TARGET_FPS && g_polygonCount < MAX_POLYGONS) {
        g_polygonCount = (int)(g_polygonCount * GROW_FACTOR);  /* +2% */
    }

    /* 安全下限 */
    if (g_polygonCount < 10) g_polygonCount = 10;
}

/* ============================================================================
 * 主循环（终端模拟版）
 * ----------------------------------------------------------------------------
 * 每秒：
 *   1. 多边形数量 +1%
 *   2. 运行自适应调整
 *   3. 打印当前状态
 * ============================================================================
 */
static void printStatus(int second)
{
    printf("  [%3ds] 多边形: %6d  |  帧数: %6.1f  |  最优临界点: %6d  |  自救次数: %d\n",
           second, g_polygonCount, g_currentFPS, g_optimalPolygons, g_recoveryCount);
}

int main(int argc, char **argv)
{
    /* OpenGL 初始化（如果使用） */
    #if USE_OPENGL
        glutInit(&argc, argv);
        glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
        glutInitWindowSize(800, 600);
        glutCreateWindow("自适应3D渲染 Demo");
        glEnable(GL_DEPTH_TEST);
        glutDisplayFunc(renderScene);
        glutIdleFunc(NULL);
    #endif

    /* ==================== 教学输出 ==================== */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║         自适应 3D 渲染教学 —— \"自我重构\" 演示             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║                                                            ║\n");
    printf("║  核心思想：                                                 ║\n");
    printf("║    多边形每秒 +1%%，当帧数跌破 60 → 自动削减回最优临界点    ║\n");
    printf("║    不靠人工预设上限，程序自己找到性能临界点                  ║\n");
    printf("║                                                            ║\n");
    printf("║  同构映射：                                                 ║\n");
    printf("║    = 知识库只放几百字思维框架，不放几十G垃圾               ║\n");
    printf("║    = 关键是找到\"刚好够用\"的最优临界点                      ║\n");
    printf("║    = 系统自己感知、自己调整，不靠死规则                     ║\n");
    printf("║                                                            ║\n");
    printf("║  规则：                                                     ║\n");
    printf("║    FPS < 20  → 紧急砍半（防死机）                          ║\n");
    printf("║    FPS < 55  → 削减 10%%（自我保护）                        ║\n");
    printf("║    FPS >= 60 → 试探 +2%%（寻找上限）                       ║\n");
    printf("║                                                            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("--- 开始模拟（每秒多边形 +1%%，观察\"卡顿→自救→稳定\"过程）---\n\n");
    /* ================================================== */

    srand((unsigned int)time(NULL));
    g_lastSecond = time(NULL);

    int second = 0;
    const int MAX_SECONDS = 35;  /* 模拟 35 秒，展示"增长→卡顿→自救→震荡"全过程 */

    while (second < MAX_SECONDS) {
        time_t now = time(NULL);

        /* 每秒执行一次：增长 + 自适应调整 */
        if (now - g_lastSecond >= 1) {
            g_lastSecond = now;
            second++;

            /* --- 每秒多边形 +1% --- */
            g_polygonCount = (int)(g_polygonCount * GROWTH_RATE);
            if (g_polygonCount > MAX_POLYGONS) g_polygonCount = MAX_POLYGONS;

            /* --- 自适应调整（核心） --- */
            adaptiveAdjust();

            /* --- 打印状态 --- */
            printStatus(second);

            /*
             * 注意：这里没有"稳定后退出"的逻辑。
             * 因为真实程序就是这样 —— 它永远在临界点附近震荡：
             *   增长 → 卡顿 → 削减 → 恢复 → 再增长 → ...
             * 这就是"动态自适应"的本质：不是一次到位，而是持续微调。
             */
        }

        #if USE_OPENGL
            /* OpenGL 模式：真实渲染循环 */
            glutMainLoop();
            break;  /* glutMainLoop 不会返回，这里只是逻辑完整性 */
        #endif
    }

    /* ==================== 总结 ==================== */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                        运行总结                             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║                                                            ║\n");
    printf("║  初始多边形:   %6d                                       ║\n", INIT_POLYGONS);
    printf("║  最终多边形:   %6d                                       ║\n", g_polygonCount);
    printf("║  最优临界点:   %6d  (程序自动找到)                        ║\n", g_optimalPolygons);
    printf("║  自我重构次数: %6d                                       ║\n", g_recoveryCount);
    printf("║                                                            ║\n");
    printf("║  结论：                                                     ║\n");
    printf("║    程序没有「预设上限」，而是自己感知卡顿、自己回退到 60 帧。  ║\n");
    printf("║    这正是「动态自适应优化」的底层逻辑。                      ║\n");
    printf("║                                                            ║\n");
    printf("║  同构回到知识库：                                           ║\n");
    printf("║    不要堆几十G资料 (= 无脑加多边形)                       ║\n");
    printf("║    先放几百字思维框架 (= 设定 60 帧目标)                    ║\n");
    printf("║    AI 自动适配你的思路  (= 自适应调整到临界点)               ║\n");
    printf("║                                                            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
