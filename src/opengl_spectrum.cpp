/*
 *  Copyright (C) 1998-2000 Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 2005-2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

/*
 *  Wed May 24 10:49:37 CDT 2000
 *  Fixes to threading/context creation for the nVidia X4 drivers by
 *  Christian Zander <phoenix@minion.de>
 */

/*
 *  Ported to XBMC by d4rk
 *  Also added 'm_hSpeed' to animate transition between bar heights
 *
 *  Ported to GLES 2.0 by Gimli
 */

#define __STDC_LIMIT_MACROS

#include <kodi/addon-instance/Visualization.h>
#include <kodi/gui/gl/GL.h>
#include <kodi/gui/gl/Shader.h>

#include <string.h>
#include <math.h>
#include <stdint.h>
#include <cstddef>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef M_PI
#define M_PI 3.141592654f
#endif

#define NUM_BANDS 16

class ATTRIBUTE_HIDDEN CVisualizationSpectrum
  : public kodi::addon::CAddonBase,
    public kodi::addon::CInstanceVisualization,
    public kodi::gui::gl::CShaderProgram
{
public:
  CVisualizationSpectrum();
  ~CVisualizationSpectrum() override = default;

  bool Start(int channels,
             int samplesPerSec,
             int bitsPerSample,
             std::string songName) override;
  void Stop() override;
  void Render() override;
  void GetInfo (bool &wantsFreq, int &syncDelay) override;
  void AudioData(const float* audioData,
                 int audioDataLength,
                 float* freqData,
                 int freqDataLength) override;
  ADDON_STATUS SetSetting(const std::string& settingName,
                          const kodi::CSettingValue& settingValue) override;

  void OnCompiledAndLinked() override;
  bool OnEnabled() override;

private:
  void SetBarHeightSetting(int settingValue);
  void SetSpeedSetting(int settingValue);
  void SetModeSetting(int settingValue);

  // Scale like human hearing loudness recognition to get
  // similar heights for all frequencies with natural sound input.
  // In other words: Scale relative to a pink-noise-spectrum aka 1/f-noise.
  // Or simply: Measure power per octave.
  // Note: Because of joined stereo, we only get 128 frequencies.

  // pFreqData[i] - joined stereo!
  // Near 1/3 octaves per bar where possible (bar# 4 to 15)
  // ** TODO ** Don't expect iFreqDataLength == 256
  int m_xscale[NUM_BANDS + 1] = {0, 2, 4, 6, 8, 10, 12, 16, 20, 26, 32, 42, 54, 68, 88, 112, 256};
  GLfloat m_hscale[NUM_BANDS];
  GLfloat m_heights[NUM_BANDS][NUM_BANDS];
  GLfloat m_cHeights[NUM_BANDS][NUM_BANDS];
  GLfloat m_scale;
  GLenum m_mode;
  float m_x_angle;
  float m_y_angle;
  float m_z_angle;
  float m_x_speed;
  float m_y_speed;
  float m_z_speed;
  float m_y_fixedAngle;

  float m_hSpeed;

  void add_quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color);
  void add_bar(GLfloat x_mid,
               GLfloat z_mid,
               GLfloat height,
               GLfloat red,
               GLfloat green,
               GLfloat blue);
  void add_bars();

  // Shader related data
  glm::mat4 m_projMat;
  glm::mat4 m_modelMat;
  GLfloat m_pointSize = 0.0f;
  std::vector<glm::vec3> m_vertex_buffer_data;
  std::vector<glm::vec3> m_color_buffer_data;

#ifdef HAS_GL
  GLuint m_vertexVBO[2] = {0};
#endif

  GLint m_uProjMatrix = -1;
  GLint m_uModelMatrix = -1;
  GLint m_uPointSize = -1;
  GLint m_hPos = -1;
  GLint m_hCol = -1;

  bool m_startOK = false;
};

CVisualizationSpectrum::CVisualizationSpectrum()
  : m_mode(GL_TRIANGLES),
    m_x_angle(20.0f),
    m_y_angle(45.0f),
    m_z_angle(0.0f),
    m_x_speed(0.0f),
    m_y_speed(0.5f),
    m_z_speed(0.0f),
    m_hSpeed(0.05f)
{
  m_scale = 1.0f;

  int freq_lo = 1;
  int freq_hi = 1;
  
  for (int x = 0; x < NUM_BANDS; x++)
  {
    // Divide by 2 and discard reminder because of joined stereo
    freq_lo = m_xscale[x] / 2 + 1; // pFreqData[0] frequency is 1 (no DC values)
    freq_hi = m_xscale[x + 1] / 2; // next band start frequency - 1
    
    m_hscale[x] = 1.0f / log2f((freq_hi + 0.5f) / (freq_lo - 0.5f)); // bands per octave
  }

  SetBarHeightSetting(kodi::GetSettingInt("bar_height"));
  SetSpeedSetting(kodi::GetSettingInt("speed"));
  SetModeSetting(kodi::GetSettingInt("mode"));
  m_y_fixedAngle = kodi::GetSettingInt("rotation_angle");

  m_vertex_buffer_data.resize(NUM_BANDS * NUM_BANDS * 6 * 2 * 3);
  m_color_buffer_data.resize(NUM_BANDS * NUM_BANDS * 6 * 2 * 3);
}

bool CVisualizationSpectrum::Start(int channels,
                                   int samplesPerSec,
                                   int bitsPerSample,
                                   std::string songName)
{
  (void)channels;
  (void)samplesPerSec;
  (void)bitsPerSample;
  (void)songName;

  std::string fraqShader = kodi::GetAddonPath("resources/shaders/" GL_TYPE_STRING "/frag.glsl");
  std::string vertShader = kodi::GetAddonPath("resources/shaders/" GL_TYPE_STRING "/vert.glsl");
  if (!LoadShaderFiles(vertShader, fraqShader) || !CompileAndLink())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to create or compile shader");
    return false;
  }

  for (int x = 0; x < NUM_BANDS; x++)
  {
    for (int y = 0; y < NUM_BANDS; y++)
    {
      m_heights[y][x] = 0.0f;
      m_cHeights[y][x] = 0.0f;
    }
  }

  m_x_speed = 0.0f;
  m_y_speed = 0.5f;
  m_z_speed = 0.0f;
  m_x_angle = 20.0f;
  m_y_angle = 45.0f;
  m_z_angle = 0.0f;

  m_projMat = glm::frustum(-1.0f, 1.0f, -1.0f, 1.0f, 1.5f, 10.0f);

#ifdef HAS_GL
  glGenBuffers(2, m_vertexVBO);
#endif

  m_startOK = true;
  return true;
}

void CVisualizationSpectrum::Stop()
{
  if (!m_startOK)
    return;

  m_startOK = false;

#ifdef HAS_GL
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(2, m_vertexVBO);
  m_vertexVBO[0] = 0;
  m_vertexVBO[1] = 0;
#endif
}

//-- Render -------------------------------------------------------------------
// Called once per frame. Do all rendering here.
//-----------------------------------------------------------------------------
void CVisualizationSpectrum::Render()
{
  if (!m_startOK)
    return;

  m_vertex_buffer_data.clear();
  m_color_buffer_data.clear();

  add_bars();

  m_x_angle = std::fmod(m_x_angle + m_x_speed, 360.0f);

  if (m_y_fixedAngle < 0.0f)
    m_y_angle = std::fmod(m_y_angle + m_y_speed, 360.0f);
  else
    m_y_angle = m_y_fixedAngle;

  m_z_angle = std::fmod(m_z_angle + m_z_speed, 360.0f);

#ifdef HAS_GL
  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[0]);
  glVertexAttribPointer(m_hPos, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 3, nullptr);
  glEnableVertexAttribArray(m_hPos);

  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[1]);
  glVertexAttribPointer(m_hCol, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 3, nullptr);
  glEnableVertexAttribArray(m_hCol);
#else
  // 1st attribute buffer : vertices
  glEnableVertexAttribArray(m_hPos);
  glVertexAttribPointer(m_hPos, 3, GL_FLOAT, GL_FALSE, 0, &m_vertex_buffer_data[0]);

  // 2nd attribute buffer : colors
  glEnableVertexAttribArray(m_hCol);
  glVertexAttribPointer(m_hCol, 3, GL_FLOAT, GL_FALSE, 0, &m_color_buffer_data[0]);
#endif

  glDisable(GL_BLEND);
#ifdef HAS_GL
  glEnable(GL_PROGRAM_POINT_SIZE);
#endif
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  // Clear the screen
  glClear(GL_DEPTH_BUFFER_BIT);

  m_modelMat = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, -5.0f));
  m_modelMat = glm::rotate(m_modelMat, glm::radians(m_x_angle), glm::vec3(1.0f, 0.0f, 0.0f));
  m_modelMat = glm::rotate(m_modelMat, glm::radians(m_y_angle), glm::vec3(0.0f, 1.0f, 0.0f));
  m_modelMat = glm::rotate(m_modelMat, glm::radians(m_z_angle), glm::vec3(0.0f, 0.0f, 1.0f));

#ifdef HAS_GL
  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[0]);
  glBufferData(GL_ARRAY_BUFFER,
               m_vertex_buffer_data.size() * sizeof(glm::vec3),
               &m_vertex_buffer_data[0],
               GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[1]);
  glBufferData(GL_ARRAY_BUFFER,
               m_color_buffer_data.size() * sizeof(glm::vec3),
               &m_color_buffer_data[0],
               GL_STATIC_DRAW);
#endif

  EnableShader();

  glDrawArrays(m_mode, 0, m_vertex_buffer_data.size());

  DisableShader();

  glDisableVertexAttribArray(m_hPos);
  glDisableVertexAttribArray(m_hCol);

  glDisable(GL_CULL_FACE);

  glDisable(GL_DEPTH_TEST);
#ifdef HAS_GL
  glDisable(GL_PROGRAM_POINT_SIZE);
#endif
  glEnable(GL_BLEND);
}

void CVisualizationSpectrum::OnCompiledAndLinked()
{
  // Variables passed directly to the Vertex shader
  m_uProjMatrix = glGetUniformLocation(ProgramHandle(), "u_projectionMatrix");
  m_uModelMatrix = glGetUniformLocation(ProgramHandle(), "u_modelViewMatrix");
  m_uPointSize = glGetUniformLocation(ProgramHandle(), "u_pointSize");
  m_hPos = glGetAttribLocation(ProgramHandle(), "a_position");
  m_hCol = glGetAttribLocation(ProgramHandle(), "a_color");
}

bool CVisualizationSpectrum::OnEnabled()
{
  // This is called after glUseProgram()
  glUniformMatrix4fv(m_uProjMatrix, 1, GL_FALSE, glm::value_ptr(m_projMat));
  glUniformMatrix4fv(m_uModelMatrix, 1, GL_FALSE, glm::value_ptr(m_modelMat));
  glUniform1f(m_uPointSize, m_pointSize);

  return true;
}

void CVisualizationSpectrum::add_quad(glm::vec3 a,
                                      glm::vec3 b,
                                      glm::vec3 c,
                                      glm::vec3 d,
                                      glm::vec3 color)
{
  m_vertex_buffer_data.push_back(a); // line-mode: 1st line
  m_vertex_buffer_data.push_back(b); //
  m_vertex_buffer_data.push_back(c);
  m_vertex_buffer_data.push_back(c);
  m_vertex_buffer_data.push_back(d); // line-mode: 2nd line
  m_vertex_buffer_data.push_back(a); //
  
  for (int i=0; i < 6; i++)
  {
    m_color_buffer_data.push_back(color);
  }
}

void CVisualizationSpectrum::add_bar(GLfloat x_mid,
                                     GLfloat z_mid,
                                     GLfloat height,
                                     GLfloat red,
                                     GLfloat green,
                                     GLfloat blue)
{
  GLfloat width = 0.1f;

  GLfloat lft = x_mid - width / 2.0f;
  GLfloat rgt = x_mid + width / 2.0f;

  GLfloat bck = z_mid - width / 2.0f;
  GLfloat fnt = z_mid + width / 2.0f;

  GLfloat top = height;
  GLfloat btm = 0.0f;
  
  glm::vec3 color = {red, green, blue};
  
  GLfloat sideMlpy1 = 1.0f;
  GLfloat sideMlpy2 = 1.0f;
  GLfloat sideMlpy3 = 1.0f;
  GLfloat sideMlpy4 = 1.0f;
  
  if (m_mode == GL_TRIANGLES)
  {
    sideMlpy1 = 0.5f;
    sideMlpy2 = 0.25f;
    sideMlpy3 = 0.75f;
    sideMlpy4 = 0.5f;
  }

  // notes:
  // Vertices must be in counter-clock-wise order for face-culling.
  // For lines-mode, only 1st <-> 2nd and 1st <-> last vertex are used.
  // Therefore the 1st vertices are choosen so that all 12 edges are drawn.
  
  // Bottom
  add_quad({rgt, btm, fnt}, {lft, btm, fnt}, {lft, btm, bck}, {rgt, btm, bck}, color);
  // Left side
  add_quad({lft, btm, fnt}, {lft, top, fnt}, {lft, top, bck}, {lft, btm, bck}, color * sideMlpy1);
  // Back
  add_quad({lft, btm, bck}, {lft, top, bck}, {rgt, top, bck}, {rgt, btm, bck}, color * sideMlpy2);
  // Front
  add_quad({rgt, top, fnt}, {lft, top, fnt}, {lft, btm, fnt}, {rgt, btm, fnt}, color * sideMlpy3);
  // Right side
  add_quad({rgt, top, bck}, {rgt, top, fnt}, {rgt, btm, fnt}, {rgt, btm, bck}, color * sideMlpy4);
  // Top
  add_quad({lft, top, bck}, {lft, top, fnt}, {rgt, top, fnt}, {rgt, top, bck}, color);
}

void CVisualizationSpectrum::add_bars()
{
  GLfloat x_mid = 0.0f;
  GLfloat z_mid = 0.0f;

  GLfloat red = 1.0f;
  GLfloat green = 1.0f;
  GLfloat blue = 1.0f;

  for (int y = 0; y < NUM_BANDS; y++)
  {
    z_mid = 3.0f * (0.5f - y / (NUM_BANDS - 1.0f));

    blue = y / (NUM_BANDS - 1.0f);

    for (int x = 0; x < NUM_BANDS; x++)
    {
      x_mid = 3.0f * (-0.5f + x / (NUM_BANDS - 1.0f));
      
      green = x / (NUM_BANDS - 1.0f);

      red = (1.0f - blue) * (1.0f - green);

      if (m_hSpeed > 0.0f && std::fabs (m_cHeights[y][x] - m_heights[y][x]) > m_hSpeed)
      {
        if (m_cHeights[y][x] < m_heights[y][x])
          m_cHeights[y][x] += m_hSpeed;
        else
          m_cHeights[y][x] -= m_hSpeed;
      }
      else
        m_cHeights[y][x] = m_heights[y][x];
      
      add_bar(x_mid, z_mid, m_cHeights[y][x], red, green, blue);
    }
  }
}

void CVisualizationSpectrum::GetInfo (bool &wantsFreq, int &syncDelay)
{
  wantsFreq = true;      // enable FFT data aka FreqData for AudioData
  syncDelay = 0;
}

void CVisualizationSpectrum::AudioData(const float* pAudioData,
                                       int iAudioDataLength,
                                       float *pFreqData,
                                       int iFreqDataLength)
{
  GLfloat h = 0.0;
  GLfloat pow = 0.0;

  for (int x = 0; x < NUM_BANDS; x++)
  {
    // Shift backwards by one row
    for (int y = NUM_BANDS - 1; y > 0; y--)
    {
      m_heights[y][x] = m_heights[y - 1][x];
    }

    // Add up the resulting output power factor avarage over time for the sine waves sum of band
    // In other words: Calculate the square of the root mean square (RMS) value
    // avg( (sin(f1)*a + sin(f2)*b + sin(f3)*c + ... )^2 ) = 0.5 * ( a^2 + b^2 + c^2 + ... )
    //  where
    //   a,b,c,... are the amplitudes aka FreqData magnitudes,
    //   0.5 is the avg of arbitrary sin(f[i])*sin(f[i]) and
    //   0.0 is the avg of arbitrary sin(f[i])*sin(f[j]) nullifying a*b and friends
    // Just add up joined stereo channels (factor 2 just gives us 3 dB more)
    pow = 0.0f;
    for (int i = m_xscale[x]; i < m_xscale[x + 1] && i < iFreqDataLength; i++)
    {
      pow += pFreqData[i] * pFreqData[i];
    }
    pow *= 0.5f;
    pow *= m_hscale[x]; // multiply with bands per octave to finally get the power per octave factor

    // CDDA-dB-scale: -96 dB/octave .. 0 dB/octave -> 0.0 .. 1.0
    h = pow > 0.0f ? 10.0f * log10f(pow) / 96.0f + 1.0f : 0.0f;

    if (h < 0.0f) // cut-off (bottom of bar)
      h = 0.0f;

    m_heights[0][x] = h * m_scale;
  }
}

void CVisualizationSpectrum::SetBarHeightSetting(int settingValue)
{
  switch (settingValue)
  {
    case 1: // standard
    {
      m_scale = 1.0f;
      break;
    }
    case 2: // big
    {
      m_scale = 2.0f;
      break;
    }
    case 3: // real big
    {
      m_scale = 3.0f;
      break;
    }
    case 4: // unused
    {
      m_scale = 0.33f;
      break;
    }
    case 0: // small
    default:
      m_scale = 0.5f;
  }
}

void CVisualizationSpectrum::SetSpeedSetting(int settingValue)
{
  switch (settingValue)
  {
    case 1:
    {
      m_hSpeed = 0.025f;
      break;
    }
    case 2:
    {
      m_hSpeed = 0.05f;
      break;
    }
    case 3:
    {
      m_hSpeed = 0.1f;
      break;
    }
    case 4:
    {
      m_hSpeed = 0.0f; // disabled (no delay)
      break;
    }
    case 0:
    default:
      m_hSpeed = 0.0125f;
  }
}

void CVisualizationSpectrum::SetModeSetting(int settingValue)
{
  switch (settingValue)
  {
    case 1:
    {
      m_mode = GL_LINES;
      m_pointSize = 0.0f;
      break;
    }
    case 2:
    {
      m_mode = GL_POINTS;
      m_pointSize = kodi::GetSettingInt("pointsize");
      break;
    }
    case 0:
    default:
    {
      m_mode = GL_TRIANGLES;
      m_pointSize = 0.0f;
    }
  }
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from Kodi)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS CVisualizationSpectrum::SetSetting(const std::string& settingName,
                                                const kodi::CSettingValue& settingValue)
{
  if (settingName.empty() || settingValue.empty())
    return ADDON_STATUS_UNKNOWN;

  if (settingName == "bar_height")
  {
    SetBarHeightSetting(settingValue.GetInt());
    return ADDON_STATUS_OK;
  }
  else if (settingName == "speed")
  {
    SetSpeedSetting(settingValue.GetInt());
    return ADDON_STATUS_OK;
  }
  else if (settingName == "mode")
  {
    SetModeSetting(settingValue.GetInt());
    return ADDON_STATUS_OK;
  }
  else if (settingName == "rotation_angle")
  {
    m_y_fixedAngle = settingValue.GetInt();
    return ADDON_STATUS_OK;
  }

  return ADDON_STATUS_UNKNOWN;
}

ADDONCREATOR(CVisualizationSpectrum)
