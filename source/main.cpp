// Dear ImGui: standalone example application for SDL2 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "string"
#include "vector"

struct image
{
    GLuint TextureID;
    int Width;
    int Height;
    unsigned char* Data;
};

struct image_entry
{
    std::string Name;
    image Image;
};

image
LoadImage(const char* Filename)
{
    image Image = {};
    // Load from file
    Image.Data = stbi_load(Filename, &Image.Width, &Image.Height, NULL, 4);
    return Image;
}

image
CopyImage(image* Other)
{
    image Result = {};
    Result.Width = Other->Width;
    Result.Height = Other->Height;
    Result.Data = (unsigned char* )malloc(Result.Width * Result.Height * 4);

    for(int Y = 0; Y < Result.Height; Y++)
    {
        for(int X = 0; X < Result.Width; X++)
        {
            ((int* )Result.Data)[X + Y * Result.Width] = *((int* )Other->Data + X + Y * Result.Width);
        }
    }

    return Result;
}

image
MakeEmptyImage(int Width, int Height, bool FillZero = true)
{
    image Result = {};
    Result.Width = Width;
    Result.Height = Height;
    Result.Data = (unsigned char* )malloc(Result.Width * Result.Height * 4);

    if(FillZero)
    {
        for(int Y = 0; Y < Result.Height; Y++)
        {
            for(int X = 0; X < Result.Width; X++)
            {
                ((int* )Result.Data)[X + Y * Result.Width] = 0;
            }
        }
    }

    return Result;
}

// Simple helper function to load an image into a OpenGL texture with common settings
bool
LoadTextureFromImage(image* Image)
{
    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Image->Width, Image->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Image->Data);

    Image->TextureID = image_texture;

    return true;
}

typedef int (*pixel_transform)(int);

int
ExtractChannel(int Pixel, int Mask)
{
    int Result = Pixel & Mask;
    return Result;
}

int ExtractRed(int Pixel)   {return ExtractChannel(Pixel, 0xFF0000FF);}
int ExtractGreen(int Pixel) {return ExtractChannel(Pixel, 0xFF00FF00);}
int ExtractBlue(int Pixel)  {return ExtractChannel(Pixel, 0xFFFF0000);}

int
GrayscaleLuminance(int Pixel)
{
    int Red   = (ExtractRed(Pixel) >> 0) & 0xFF;
    int Green = (ExtractGreen(Pixel) >> 8) & 0xFF;
    int Blue  = (ExtractBlue(Pixel) >> 16) & 0xFF;

#if 1
    float RedFactor = 0.2126;
    float GreenFactor = 0.7152;
    float BlueFactor = 0.0722;
#else
    float RedFactor = 0.299;
    float GreenFactor = 0.587;
    float BlueFactor = 0.114;
#endif

    Red *= RedFactor;
    Green *= GreenFactor;
    Blue *= BlueFactor;

    int Result = (0xFF000000) |
                 (Red << 0) |
                 (Green << 8) |
                 (Blue << 16);

    return Result;
}

int
GrayscaleAverage(int Pixel)
{
    int Red   = (ExtractRed(Pixel) >> 0) & 0xFF;
    int Green = (ExtractGreen(Pixel) >> 8) & 0xFF;
    int Blue  = (ExtractBlue(Pixel) >> 16) & 0xFF;

    int Average = (Red + Green + Blue) / 3;
    Red = Green = Blue = Average;

    int Result = (0xFF000000) |
                 (Red << 0) |
                 (Green << 8) |
                 (Blue << 16);

    return Result;
}

void
ApplyTransform(image* Image, pixel_transform Transform)
{
    for(int Y = 0; Y < Image->Height; Y++)
    {
        for(int X = 0; X < Image->Width; X++)
        {
            int* Pixel = (int* )&Image->Data[(X + Y * Image->Width) * 4];
            *Pixel = Transform(*Pixel);
        }
    }
}

// Returns index of added image entry
size_t
AddImageEntry(std::vector<image_entry>& Images, std::string Name, image Image)
{
    image_entry Entry = {};
    Entry.Name = Name;
    Entry.Image = Image;
    Images.push_back(Entry);
    return Images.size() - 1;
}

struct kernel
{
    int Width;
    int Height;
    float* Data;
};

kernel
MakeNewKernel(int Width, int Height)
{
    // Only odd dimensional kernel allowed
    assert(Width % 2 == 1);
    assert(Height % 2 == 1);

    kernel Result = {};
    Result.Width = Width;
    Result.Height = Height;
    Result.Data = (float* )malloc(Result.Width * Result.Height * sizeof(float));

    for(int Y = 0; Y < Result.Height; Y++)
    {
        for(int X = 0; X < Result.Width; X++)
        {
            Result.Data[X + Y * Result.Width] = 0;
        }
    }

    return Result;
}

int
Clamp(int Value, int Min, int Max)
{
    int Result = Value;
    if(Result < Min)
        Result = Min;
    else if (Result > Max)
        Result = Max;

    return Result;
}

// NOTE: This allocates new image
image
ApplyConvolution(image* Image, kernel* Kernel)
{
    image Result = MakeEmptyImage(Image->Width, Image->Height);
    for(int Y = 0; Y < Result.Height; Y++)
    {
        for(int X = 0; X < Result.Width; X++)
        {
            int RedValue = 0;
            int GreenValue = 0;
            int BlueValue = 0;

            for(int Dy = -Kernel->Height/2; Dy <= Kernel->Height/2; Dy++)
            {
                for(int Dx = -Kernel->Width/2; Dx <= Kernel->Width/2; Dx++)
                {
                    int XCoord = Clamp(X + Dx, 0, Result.Width);
                    int YCoord = Clamp(Y + Dy, 0, Result.Height);

                    int Pixel = ((int* )Image->Data)[XCoord + YCoord * Result.Width];

                    int Red =   (ExtractRed(Pixel) >> 0) & 0xFF;
                    int Green = (ExtractGreen(Pixel) >> 8) & 0xFF;
                    int Blue =  (ExtractBlue(Pixel) >> 16) & 0xFF;

                    RedValue   += Red * Kernel->Data[(Dx + Kernel->Width/2) + (Dy + Kernel->Height/2) * Kernel->Width];
                    GreenValue += Green * Kernel->Data[(Dx + Kernel->Width/2) + (Dy + Kernel->Height/2) * Kernel->Width];
                    BlueValue  += Blue * Kernel->Data[(Dx + Kernel->Width/2) + (Dy + Kernel->Height/2) * Kernel->Width];
                }
            }

            RedValue = Clamp(RedValue, 0, 255);
            GreenValue = Clamp(GreenValue, 0, 255);
            BlueValue = Clamp(BlueValue, 0, 255);

            int ResultPixel = (0xFF000000) |
                              (RedValue << 0) |
                              (GreenValue << 8) |
                              (BlueValue << 16);

            ((int* )Result.Data)[X + Y * Result.Width] = ResultPixel;
        }
    }

    return Result;
}

// Main code
int main(int, char**)
{
    // Setup SDL
    // (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
    // depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to latest version of SDL is recommended!)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Arial.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    IM_ASSERT(font != NULL);

    // Populating array with some images
    std::vector<image_entry> Images;
    size_t EntryIndex = AddImageEntry(Images, "Original", LoadImage("corgi.jpg"));
    bool Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Extract red channel
    EntryIndex = AddImageEntry(Images, "Extract red", CopyImage(&Images[0].Image));
    ApplyTransform(&Images[EntryIndex].Image, ExtractRed);
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Extract green channel
    EntryIndex = AddImageEntry(Images, "Extract green", CopyImage(&Images[0].Image));
    ApplyTransform(&Images[EntryIndex].Image, ExtractGreen);
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Extract blue channel
    EntryIndex = AddImageEntry(Images, "Extract blue", CopyImage(&Images[0].Image));
    ApplyTransform(&Images[EntryIndex].Image, ExtractBlue);
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Grayscale by averaging each channel
    EntryIndex = AddImageEntry(Images, "Grayscale average", CopyImage(&Images[0].Image));
    int GrayscaleEntryIndex = EntryIndex; // will use later for edge detection
    ApplyTransform(&Images[EntryIndex].Image, GrayscaleAverage);
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Gaussian blur 3x3
    kernel GaussianKernel3x3 = MakeNewKernel(3, 3);
    GaussianKernel3x3.Data[0] = (float)1/(float)16;
    GaussianKernel3x3.Data[1] = (float)2/(float)16;
    GaussianKernel3x3.Data[2] = (float)1/(float)16;
    GaussianKernel3x3.Data[3] = (float)2/(float)16;
    GaussianKernel3x3.Data[4] = (float)3/(float)16;
    GaussianKernel3x3.Data[5] = (float)2/(float)16;
    GaussianKernel3x3.Data[6] = (float)1/(float)16;
    GaussianKernel3x3.Data[7] = (float)2/(float)16;
    GaussianKernel3x3.Data[8] = (float)1/(float)16;
    EntryIndex = AddImageEntry(Images, "Gaussian blur 3x3", ApplyConvolution(&Images[0].Image, &GaussianKernel3x3));
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Gaussian blur 5x5
    kernel GaussianKernel5x5 = MakeNewKernel(5, 5);
    GaussianKernel5x5.Data[0]  = (float)1/(float)256;
    GaussianKernel5x5.Data[1]  = (float)4/(float)256;
    GaussianKernel5x5.Data[2]  = (float)6/(float)256;
    GaussianKernel5x5.Data[3]  = (float)4/(float)256;
    GaussianKernel5x5.Data[4]  = (float)1/(float)256;
    GaussianKernel5x5.Data[5]  = (float)4/(float)256;
    GaussianKernel5x5.Data[6]  = (float)16/(float)256;
    GaussianKernel5x5.Data[7]  = (float)24/(float)256;
    GaussianKernel5x5.Data[8]  = (float)16/(float)256;
    GaussianKernel5x5.Data[9]  = (float)4/(float)256;
    GaussianKernel5x5.Data[10] = (float)6/(float)256;
    GaussianKernel5x5.Data[11] = (float)24/(float)256;
    GaussianKernel5x5.Data[12] = (float)36/(float)256;
    GaussianKernel5x5.Data[13] = (float)24/(float)256;
    GaussianKernel5x5.Data[14] = (float)6/(float)256;
    GaussianKernel5x5.Data[15] = (float)4/(float)256;
    GaussianKernel5x5.Data[16] = (float)16/(float)256;
    GaussianKernel5x5.Data[17] = (float)24/(float)256;
    GaussianKernel5x5.Data[18] = (float)16/(float)256;
    GaussianKernel5x5.Data[19] = (float)4/(float)256;
    GaussianKernel5x5.Data[20] = (float)1/(float)256;
    GaussianKernel5x5.Data[21] = (float)4/(float)256;
    GaussianKernel5x5.Data[22] = (float)6/(float)256;
    GaussianKernel5x5.Data[23] = (float)4/(float)256;
    GaussianKernel5x5.Data[24] = (float)1/(float)256;
    EntryIndex = AddImageEntry(Images, "Gaussian blur 5x5", ApplyConvolution(&Images[0].Image, &GaussianKernel5x5));
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Edge detection (horizontal)
    kernel SobelHorizontal = MakeNewKernel(3, 3);
    float SobelFactor = 2.0;
    SobelHorizontal.Data[0]  = SobelFactor * (float)1/(float)4;
    SobelHorizontal.Data[1]  = SobelFactor * 0;
    SobelHorizontal.Data[2]  = SobelFactor * (float)-1/(float)4;
    SobelHorizontal.Data[3]  = SobelFactor * (float)2/(float)4;
    SobelHorizontal.Data[4]  = SobelFactor * 0;
    SobelHorizontal.Data[5]  = SobelFactor * (float)-2/(float)4;
    SobelHorizontal.Data[6]  = SobelFactor * (float)1/(float)4;
    SobelHorizontal.Data[7]  = SobelFactor * 0;
    SobelHorizontal.Data[8]  = SobelFactor * (float)-1/(float)4;
    EntryIndex = AddImageEntry(Images, "Edge detection (horizontal)", ApplyConvolution(&Images[GrayscaleEntryIndex].Image, &SobelHorizontal));
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);

    // Edge detection (vertical)
    kernel SobelVertical = MakeNewKernel(3, 3);
    SobelVertical.Data[0]  = SobelFactor * (float)1/(float)4;
    SobelVertical.Data[1]  = SobelFactor * (float)2/(float)4;
    SobelVertical.Data[2]  = SobelFactor * (float)1/(float)4;
    SobelVertical.Data[3]  = SobelFactor * 0;
    SobelVertical.Data[4]  = SobelFactor * 0;
    SobelVertical.Data[5]  = SobelFactor * 0;
    SobelVertical.Data[6]  = SobelFactor * (float)-1/(float)4;
    SobelVertical.Data[7]  = SobelFactor * (float)-2/(float)4;
    SobelVertical.Data[8]  = SobelFactor * (float)-1/(float)4;
    EntryIndex = AddImageEntry(Images, "Edge detection (vertical)", ApplyConvolution(&Images[GrayscaleEntryIndex].Image, &SobelVertical));
    Ret = LoadTextureFromImage(&Images[EntryIndex].Image);
    assert(Ret);


    // Main loop
    bool Running = true;
    while (Running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch(event.type)
            {
                case SDL_QUIT:
                {
                    Running = false;
                } break;

                case SDL_KEYDOWN:
                {
                    if(event.key.keysym.sym == SDLK_ESCAPE)
                    {
                        Running = false;
                    }
                } break;
            }

            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                Running = false;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();


        ImGui::Begin("Original");
        ImGui::Image((void*)(intptr_t)Images[0].Image.TextureID, ImVec2(Images[0].Image.Width, Images[0].Image.Height));
        ImGui::End();

        ImGui::Begin("Images");

        for(image_entry& Entry : Images)
        {
            if(ImGui::CollapsingHeader(Entry.Name.c_str()))
            {
                // ImGui::Text("Name = %s", Entry.Name);
                image& Img = Entry.Image;
                // ImGui::Text("size = %d x %d", Img.Width, Img.Height);
                ImGui::Image((void*)(intptr_t)Img.TextureID, ImVec2(Img.Width, Img.Height));
            }
        }

        ImGui::End();

        // Rendering
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
