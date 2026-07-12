/*
 * ╔══════════════════════════════════════════════════════════╗
 * ║   AgroSense — Decision Tree (auto-generated)             ║
 * ║   Training accuracy : 88.4%                              ║
 * ║   CV accuracy       : 82.4% ± 3.5%                      ║
 * ║   Samples           : 6400                               ║
 * ║   Features          : moisture_pct, texture_code,        ║
 * ║                       growth_stage                       ║
 * ║   Classes           : 0=HOLD  1=IRRIGATE  2=URGENT       ║
 * ╚══════════════════════════════════════════════════════════╝
 *
 * Texture codes:  0=sandy  1=silty  2=loamy  3=clay
 * Growth stages:  0=germination  1=vegetative
 *                 2=tasseling    3=grain fill
 *
 * Usage:
 *   int action = decide(moisture, "loamy", 1);
 *   const char* name = actionName(action);
 *   float thresh = getThreshold("loamy");
 */

#ifndef DECISION_TREE_RULES_H
#define DECISION_TREE_RULES_H

#include <string.h>   // strcmp

// ── Action codes ──────────────────────────────
#define ACTION_HOLD     0
#define ACTION_IRRIGATE 1
#define ACTION_URGENT   2

// ── Texture codes ─────────────────────────────
#define TEXTURE_SANDY 0
#define TEXTURE_SILTY 1
#define TEXTURE_LOAMY 2
#define TEXTURE_CLAY  3

// ── Growth stage codes ────────────────────────
#define STAGE_GERMINATION 0
#define STAGE_VEGETATIVE  1
#define STAGE_TASSELING   2
#define STAGE_GRAIN_FILL  3

// ════════════════════════════════════════════════
//  IRRIGATION THRESHOLDS
//  Returns the moisture % below which irrigation
//  should be triggered for a given soil texture.
// ════════════════════════════════════════════════
inline float getThreshold(const char* texture) {
  if (strcmp(texture, "sandy") == 0) return 60.0f;
  if (strcmp(texture, "silty") == 0) return 40.0f;
  if (strcmp(texture, "loamy") == 0) return 45.0f;
  if (strcmp(texture, "clay")  == 0) return 35.0f;
  return 45.0f;  // default: loamy
}

// ════════════════════════════════════════════════
//  TEXTURE NAME → CODE
// ════════════════════════════════════════════════
inline int textureNameToCode(const char* texture) {
  if (strcmp(texture, "sandy") == 0) return TEXTURE_SANDY;
  if (strcmp(texture, "silty") == 0) return TEXTURE_SILTY;
  if (strcmp(texture, "loamy") == 0) return TEXTURE_LOAMY;
  if (strcmp(texture, "clay")  == 0) return TEXTURE_CLAY;
  return TEXTURE_LOAMY;  // default
}

// ════════════════════════════════════════════════
//  DECISION TREE
//  Trained on 6400 samples — soil moisture +
//  texture + growth stage → irrigation action.
//
//  Returns: ACTION_HOLD | ACTION_IRRIGATE | ACTION_URGENT
// ════════════════════════════════════════════════
inline int decisionTree(float moisture, int textureCode, int growthStage) {
  if (moisture <= 40.95f) {
    if (moisture <= 17.75f) {
      if (moisture <= 13.65f) {
        return ACTION_URGENT;
      } else {
        if (growthStage <= 1) {
          return ACTION_URGENT;
        } else {
          return ACTION_URGENT;
        }
      }
    } else {
      if (textureCode <= 1) {
        if (moisture <= 30.45f) {
          if (growthStage <= 1) {
            return ACTION_IRRIGATE;
          } else {
            return ACTION_URGENT;
          }
        } else {
          return ACTION_IRRIGATE;
        }
      } else {
        if (moisture <= 22.75f) {
          if (growthStage <= 2) {
            return ACTION_IRRIGATE;
          } else {
            return ACTION_URGENT;
          }
        } else {
          return ACTION_IRRIGATE;
        }
      }
    }
  } else {
    if (moisture <= 54.05f) {
      if (textureCode <= 1) {
        if (growthStage <= 1) {
          if (moisture <= 42.80f) {
            return ACTION_IRRIGATE;
          } else {
            return ACTION_HOLD;
          }
        } else {
          return ACTION_IRRIGATE;
        }
      } else {
        if (growthStage <= 2) {
          return ACTION_HOLD;
        } else {
          if (moisture <= 49.65f) {
            return ACTION_IRRIGATE;
          } else {
            return ACTION_HOLD;
          }
        }
      }
    } else {
      if (moisture <= 65.95f) {
        if (textureCode <= 1) {
          if (growthStage <= 2) {
            return ACTION_HOLD;
          } else {
            return ACTION_IRRIGATE;
          }
        } else {
          return ACTION_HOLD;
        }
      } else {
        return ACTION_HOLD;
      }
    }
  }
}

// ════════════════════════════════════════════════
//  MAIN ENTRY POINT
//  Call this from the firmware:
//    int action = decide(moisture, "loamy", 1);
// ════════════════════════════════════════════════
inline int decide(float moisture, const char* texture, int growthStage = 1) {
  return decisionTree(moisture, textureNameToCode(texture), growthStage);
}

// ════════════════════════════════════════════════
//  ACTION → STRING  (for Firebase / Serial)
// ════════════════════════════════════════════════
inline const char* actionName(int action) {
  switch (action) {
    case ACTION_URGENT:   return "URGENT";
    case ACTION_IRRIGATE: return "IRRIGATE";
    default:              return "HOLD";
  }
}

#endif  // DECISION_TREE_RULES_H