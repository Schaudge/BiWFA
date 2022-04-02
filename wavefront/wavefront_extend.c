/*
 *                             The MIT License
 *
 * Wavefront Alignment Algorithms
 * Copyright (c) 2017 by Santiago Marco-Sola  <santiagomsola@gmail.com>
 *
 * This file is part of Wavefront Alignment Algorithms.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * PROJECT: Wavefront Alignment Algorithms
 * AUTHOR(S): Santiago Marco-Sola <santiagomsola@gmail.com>
 * DESCRIPTION: WaveFront-Alignment module for the "extension" of exact matches
 */

#include "utils/string_padded.h"
#include "wavefront_extend.h"
#include "wavefront_align.h"
#include "wavefront_compute.h"
#include "wavefront_heuristic.h"

/*
 * Termination (detect end of alignment)
 */
bool wavefront_extend_end2end_check_termination(
    wavefront_aligner_t* const wf_aligner,
    wavefront_t* const mwavefront,
    const int score,
    const int score_mod) {
  // Parameters
  const int pattern_length = wf_aligner->pattern_length;
  const int text_length = wf_aligner->text_length;
  const affine_matrix_type component_end = wf_aligner->component_end;
  const int alignment_k = DPMATRIX_DIAGONAL(text_length,pattern_length);
  const wf_offset_t alignment_offset = DPMATRIX_OFFSET(text_length,pattern_length);
  // Alignment ends in M
  if (component_end == affine_matrix_M) {
    // Check diagonal/offset
    if (mwavefront->lo > alignment_k || alignment_k > mwavefront->hi) return false; // Not done
    const wf_offset_t moffset = mwavefront->offsets[alignment_k];
    if (moffset < alignment_offset) return false; // Not done
    // We are done
    wf_aligner->alignment_end_pos.score = score;
    wf_aligner->alignment_end_pos.k = alignment_k;
    wf_aligner->alignment_end_pos.offset = alignment_offset;
    return true;
  } else if (component_end == affine_matrix_I) {
    // Fetch I-wavefront & check diagonal/offset
    wavefront_t* const iwavefront = wf_aligner->wf_components.i1wavefronts[score_mod];
    if (iwavefront == NULL || iwavefront->lo > alignment_k || alignment_k > iwavefront->hi) return false; // Not done
    const wf_offset_t ioffset = iwavefront->offsets[alignment_k];
    if (ioffset < alignment_offset) return false; // Not done
    // We are done
    wf_aligner->alignment_end_pos.score = score;
    wf_aligner->alignment_end_pos.k = alignment_k;
    wf_aligner->alignment_end_pos.offset = alignment_offset;
    return true;
  } else { // wf_aligner->end_component == affine_matrix_D
    // Fetch D-wavefront & check diagonal/offset
    wavefront_t* const dwavefront = wf_aligner->wf_components.d1wavefronts[score_mod];
    if (dwavefront == NULL || dwavefront->lo > alignment_k || alignment_k > dwavefront->hi) return false; // Not done
    const wf_offset_t doffset = dwavefront->offsets[alignment_k];
    if (doffset < alignment_offset) return false; // Not done
    // We are done
    wf_aligner->alignment_end_pos.score = score;
    wf_aligner->alignment_end_pos.k = alignment_k;
    wf_aligner->alignment_end_pos.offset = alignment_offset;
    return true;
  }
}
bool wavefront_extend_endsfree_check_termination(
    wavefront_aligner_t* const wf_aligner,
    wavefront_t* const mwavefront,
    const int score,
    const int k,
    const wf_offset_t offset) {
  // Parameters
  const int pattern_length = wf_aligner->pattern_length;
  const int text_length = wf_aligner->text_length;
  // Check ends-free reaching boundaries
  const int h_pos = WAVEFRONT_H(k,offset);
  const int v_pos = WAVEFRONT_V(k,offset);
  if (h_pos >= text_length) { // Text is aligned
    // Is Pattern end-free?
    const int pattern_left = pattern_length - v_pos;
    const int pattern_end_free = wf_aligner->alignment_form.pattern_end_free;
    if (pattern_left <= pattern_end_free) {
      wf_aligner->alignment_end_pos.score = score;
      wf_aligner->alignment_end_pos.k = k;
      wf_aligner->alignment_end_pos.offset = offset;
      return true; // Quit (we are done)
    }
  }
  if (v_pos >= pattern_length) { // Pattern is aligned
    // Is text end-free?
    const int text_left = text_length - h_pos;
    const int text_end_free = wf_aligner->alignment_form.text_end_free;
    if (text_left <= text_end_free) {
      wf_aligner->alignment_end_pos.score = score;
      wf_aligner->alignment_end_pos.k = k;
      wf_aligner->alignment_end_pos.offset = offset;
      return true; // Quit (we are done)
    }
  }
  // Not done
  return false;
}
/*
 * "Extension" functions (comparing characters)
 */
int wavefront_extend_matches_packed(
    wavefront_aligner_t* const wf_aligner,
    const int score,
    const int score_mod,
    const bool endsfree,
    const bool compute_max) {
  // Fetch m-wavefront
  wavefront_t* const mwavefront = wf_aligner->wf_components.mwavefronts[score_mod];
  if (mwavefront==NULL) return false;
  // Extend diagonally each wavefront point
  wf_offset_t* const offsets = mwavefront->offsets;
  const int lo = mwavefront->lo;
  const int hi = mwavefront->hi;
  wf_offset_t max_antidiag = 0;
  int k;
  for (k=lo;k<=hi;++k) {
    // Check offset & positions
    //   - No offset should be out of boundaries !(h>tlen,v>plen)
    //   - if (h==tlen,v==plen) extension won't increment (sentinels)
    wf_offset_t offset = offsets[k];
    if (offset == WAVEFRONT_OFFSET_NULL) continue;
    // Fetch pattern/text blocks
    uint64_t* pattern_blocks = (uint64_t*)(wf_aligner->pattern+WAVEFRONT_V(k,offset));
    uint64_t* text_blocks = (uint64_t*)(wf_aligner->text+WAVEFRONT_H(k,offset));
    // Compare 64-bits blocks
    uint64_t cmp = *pattern_blocks ^ *text_blocks;
    while (__builtin_expect(cmp==0,0)) {
      // Increment offset (full block)
      offset += 8;
      // Next blocks
      ++pattern_blocks;
      ++text_blocks;
      // Compare
      cmp = *pattern_blocks ^ *text_blocks;
    }
    // Count equal characters
    const int equal_right_bits = __builtin_ctzl(cmp);
    const int equal_chars = DIV_FLOOR(equal_right_bits,8);
    offset += equal_chars;
    // Update offset
    offsets[k] = offset;
    // Compute max
    if (compute_max) {
       const wf_offset_t antidiag = WAVEFRONT_ANTIDIAGONAL(k,offset);
      if (max_antidiag < antidiag) max_antidiag = antidiag;
    }
    // Check ends-free reaching boundaries
    if (endsfree && wavefront_extend_endsfree_check_termination(wf_aligner,mwavefront,score,k,offset)) {
      return 1; // Quit (we are done)
    }
  }
  // If compute-max flag, return maximum antidiagonal (bialign)
  if (compute_max) {
    return max_antidiag;
  }
  // Check end-to-end finished
  if (!endsfree) {
    return wavefront_extend_end2end_check_termination(wf_aligner,mwavefront,score,score_mod);
  }
  // Alignment not finished
  return 0;
}
bool wavefront_extend_matches_custom(
    wavefront_aligner_t* const wf_aligner,
    const int score,
    const int score_mod,
    const bool endsfree) {
  // Fetch m-wavefront
  wavefront_t* const mwavefront = wf_aligner->wf_components.mwavefronts[score_mod];
  if (mwavefront==NULL) return false;
  // Parameters (custom matching function)
  alignment_match_funct_t match_funct = wf_aligner->match_funct;
  void* const func_arguments = wf_aligner->match_funct_arguments;
  // Extend diagonally each wavefront point
  wf_offset_t* const offsets = mwavefront->offsets;
  const int lo = mwavefront->lo;
  const int hi = mwavefront->hi;
  int k;
  for (k=lo;k<=hi;++k) {
    // Check offset
    wf_offset_t offset = offsets[k];
    if (offset == WAVEFRONT_OFFSET_NULL) continue;
    // Count equal characters
    int v = WAVEFRONT_V(k,offset);
    int h = WAVEFRONT_H(k,offset);
    while (match_funct(v,h,func_arguments)) {
      h++; v++; offset++;
    }
    // Update offset
    offsets[k] = offset;
    // Check ends-free reaching boundaries
    if (endsfree && wavefront_extend_endsfree_check_termination(wf_aligner,mwavefront,score,k,offset)) {
      return true; // Quit (we are done)
    }
  }
  // Check end-to-end finished
  if (!endsfree) {
    return wavefront_extend_end2end_check_termination(wf_aligner,mwavefront,score,score_mod);
  }
  // Alignment not finished
  return false;
}
/*
 * Wavefront exact "extension"
 */
int wavefront_extend_end2end(
    wavefront_aligner_t* const wf_aligner,
    const int score) {
  // Compute score
  const bool memory_modular = wf_aligner->wf_components.memory_modular;
  const int max_score_scope = wf_aligner->wf_components.max_score_scope;
  const int score_mod = (memory_modular) ? score % max_score_scope : score;
  // Extend wavefront
  const int end_reached = wavefront_extend_matches_packed(wf_aligner,score,score_mod,false,false);
  if (end_reached) {
    wf_aligner->align_status.status = WF_STATUS_SUCCESSFUL;
    return 1; // Done
  }
  // Cut-off wavefront heuristically
  if (wf_aligner->heuristic.strategy != wf_heuristic_none) {
    const bool alignment_dropped = wavefront_heuristic_cufoff(wf_aligner,score);
    if (alignment_dropped) {
      wf_aligner->align_status.status = WF_STATUS_HEURISTICALY_DROPPED;
      return 1; // Done
    }
  }
  return 0; // Not done
}
int wavefront_extend_end2end_max(
    wavefront_aligner_t* const wf_aligner,
    const int score) {
  // Compute score
  const bool memory_modular = wf_aligner->wf_components.memory_modular;
  const int max_score_scope = wf_aligner->wf_components.max_score_scope;
  const int score_mod = (memory_modular) ? score % max_score_scope : score;
  // Extend wavefront & return max
  return wavefront_extend_matches_packed(wf_aligner,score,score_mod,false,true);
}
int wavefront_extend_endsfree(
    wavefront_aligner_t* const wf_aligner,
    const int score) {
  // Compute score
  const bool memory_modular = wf_aligner->wf_components.memory_modular;
  const int max_score_scope = wf_aligner->wf_components.max_score_scope;
  const int score_mod = (memory_modular) ? score % max_score_scope : score;
  // Extend wavefront
  const int end_reached = wavefront_extend_matches_packed(wf_aligner,score,score_mod,true,false);
  if (end_reached) {
    wf_aligner->align_status.status = WF_STATUS_SUCCESSFUL;
    return 1; // Done
  }
  // Cut-off wavefront heuristically
  if (wf_aligner->heuristic.strategy != wf_heuristic_none) {
    const bool alignment_dropped = wavefront_heuristic_cufoff(wf_aligner,score);
    if (alignment_dropped) {
      wf_aligner->align_status.status = WF_STATUS_HEURISTICALY_DROPPED;
      return 1; // Done
    }
  }
  return 0; // Not done
}
int wavefront_extend_custom(
    wavefront_aligner_t* const wf_aligner,
    const int score) {
  // Compute score
  const bool memory_modular = wf_aligner->wf_components.memory_modular;
  const int max_score_scope = wf_aligner->wf_components.max_score_scope;
  const int score_mod = (memory_modular) ? score % max_score_scope : score;
  // Extend wavefront
  const bool endsfree = (wf_aligner->alignment_form.span == alignment_endsfree);
  const bool end_reached = wavefront_extend_matches_custom(wf_aligner,score,score_mod,endsfree);
  if (end_reached) {
    wf_aligner->align_status.status = WF_STATUS_SUCCESSFUL;
    return 1; // Done
  }
  // Cut-off wavefront heuristically
  if (wf_aligner->heuristic.strategy != wf_heuristic_none) {
    const bool alignment_dropped = wavefront_heuristic_cufoff(wf_aligner,score);
    if (alignment_dropped) {
      wf_aligner->align_status.status = WF_STATUS_HEURISTICALY_DROPPED;
      return 1; // Done
    }
  }
  return 0; // Not done
}


