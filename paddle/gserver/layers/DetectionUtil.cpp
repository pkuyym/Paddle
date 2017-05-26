/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "DetectionUtil.h"

namespace paddle {

size_t appendWithPermute(const MatrixPtr inMatrix,
                         size_t height,
                         size_t width,
                         size_t outTotalSize,
                         size_t outOffset,
                         size_t batchSize,
                         MatrixPtr outMatrix,
                         PermMode permMode,
                         bool useGpu) {
  if (permMode == NCHWTONHWC) {
    size_t inElementCnt = inMatrix->getElementCnt();
    size_t channels = inElementCnt / (height * width * batchSize);
    size_t imgSize = height * width;
    for (size_t i = 0; i < batchSize; ++i) {
      outOffset += i * (outTotalSize / batchSize);
      const MatrixPtr inTmp =
          Matrix::create(inMatrix->getData() + i * channels * imgSize,
                         channels,
                         imgSize,
                         false,
                         useGpu);
      MatrixPtr outTmp = Matrix::create(
          outMatrix->getData() + outOffset, imgSize, channels, false, useGpu);
      inTmp->transpose(outTmp, false);
    }
    return channels * imgSize;
  } else {
    LOG(FATAL) << "Unkown permute mode";
  }
}

size_t decomposeWithPermute(const MatrixPtr inMatrix,
                            size_t height,
                            size_t width,
                            size_t inTotalSize,
                            size_t inOffset,
                            size_t batchSize,
                            MatrixPtr outMatrix,
                            PermMode permMode,
                            bool useGpu) {
  if (permMode == NHWCTONCHW) {
    size_t outElementCnt = outMatrix->getElementCnt();
    size_t channels = outElementCnt / (height * width * batchSize);
    size_t imgSize = height * width;
    for (size_t i = 0; i < batchSize; ++i) {
      inOffset += i * (inTotalSize / batchSize);
      const MatrixPtr inTmp = Matrix::create(
          inMatrix->getData() + inOffset, imgSize, channels, false, useGpu);
      MatrixPtr outTmp =
          Matrix::create(outMatrix->getData() + i * channels * imgSize,
                         channels,
                         imgSize,
                         false,
                         useGpu);
      inTmp->transpose(outTmp, false);
    }
    return channels * imgSize;
  } else {
    LOG(FATAL) << "Unkown permute mode";
  }
}

real jaccardOverlap(const NormalizedBBox& bbox1, const NormalizedBBox& bbox2) {
  if (bbox2.xMin > bbox1.xMax || bbox2.xMax < bbox1.xMin ||
      bbox2.yMin > bbox1.yMax || bbox2.yMax < bbox1.yMin) {
    return 0.0;
  } else {
    real interXMin = std::max(bbox1.xMin, bbox2.xMin);
    real interYMin = std::max(bbox1.yMin, bbox2.yMin);
    real interXMax = std::min(bbox1.xMax, bbox2.xMax);
    real interYMax = std::min(bbox1.yMax, bbox2.yMax);

    real interWidth = interXMax - interXMin;
    real interHeight = interYMax - interYMin;
    real interSize = interWidth * interHeight;

    real bboxSize1 = bbox1.getSize();
    real bboxSize2 = bbox2.getSize();

    return interSize / (bboxSize1 + bboxSize2 - interSize);
  }
}

vector<real> encodeBBoxWithVar(const NormalizedBBox& priorBBox,
                               const vector<real> priorBBoxVar,
                               const NormalizedBBox& gtBBox) {
  real priorBBoxWidth = priorBBox.getWidth();
  real priorBBoxHeight = priorBBox.getHeight();
  real priorBBoxCenterX = priorBBox.getCenterX();
  real priorBBoxCenterY = priorBBox.getCenterY();

  real gtBBoxWidth = gtBBox.getWidth();
  real gtBBoxHeight = gtBBox.getHeight();
  real gtBBoxCenterX = gtBBox.getCenterX();
  real gtBBoxCenterY = gtBBox.getCenterY();

  vector<real> offsetParam;
  offsetParam.push_back((gtBBoxCenterX - priorBBoxCenterX) / priorBBoxWidth /
                        priorBBoxVar[0]);
  offsetParam.push_back((gtBBoxCenterY - priorBBoxCenterY) / priorBBoxHeight /
                        priorBBoxVar[1]);
  offsetParam.push_back(std::log(std::fabs(gtBBoxWidth / priorBBoxWidth)) /
                        priorBBoxVar[2]);
  offsetParam.push_back(std::log(std::fabs(gtBBoxHeight / priorBBoxHeight)) /
                        priorBBoxVar[3]);

  return offsetParam;
}

NormalizedBBox decodeBBoxWithVar(const NormalizedBBox& priorBBox,
                                 const vector<real>& priorBBoxVar,
                                 const vector<real>& locPredData) {
  real priorBBoxWidth = priorBBox.getWidth();
  real priorBBoxHeight = priorBBox.getHeight();
  real priorBBoxCenterX = priorBBox.getCenterX();
  real priorBBoxCenterY = priorBBox.getCenterY();

  real decodedBBoxCenterX =
      priorBBoxVar[0] * locPredData[0] * priorBBoxWidth + priorBBoxCenterX;
  real decodedBBoxCenterY =
      priorBBoxVar[1] * locPredData[1] * priorBBoxHeight + priorBBoxCenterY;
  real decodedBBoxWidth =
      std::exp(priorBBoxVar[2] * locPredData[2]) * priorBBoxWidth;
  real decodedBBoxHeight =
      std::exp(priorBBoxVar[3] * locPredData[3]) * priorBBoxHeight;

  NormalizedBBox decodedBBox;
  decodedBBox.xMin = decodedBBoxCenterX - decodedBBoxWidth / 2;
  decodedBBox.yMin = decodedBBoxCenterY - decodedBBoxHeight / 2;
  decodedBBox.xMax = decodedBBoxCenterX + decodedBBoxWidth / 2;
  decodedBBox.yMax = decodedBBoxCenterY + decodedBBoxHeight / 2;

  return decodedBBox;
}

void getBBoxFromPriorData(const real* priorData,
                          const size_t numBBoxes,
                          vector<NormalizedBBox>& bboxVec) {
  size_t outOffset = bboxVec.size();
  bboxVec.resize(bboxVec.size() + numBBoxes);
  for (size_t i = 0; i < numBBoxes; ++i) {
    NormalizedBBox bbox;
    bbox.xMin = *(priorData + i * 8);
    bbox.yMin = *(priorData + i * 8 + 1);
    bbox.xMax = *(priorData + i * 8 + 2);
    bbox.yMax = *(priorData + i * 8 + 3);
    bboxVec[outOffset + i] = bbox;
  }
}

void getBBoxVarFromPriorData(const real* priorData,
                             const size_t num,
                             vector<vector<real>>& varVec) {
  size_t outOffset = varVec.size();
  varVec.resize(varVec.size() + num);
  for (size_t i = 0; i < num; ++i) {
    vector<real> var;
    var.push_back(*(priorData + i * 8 + 4));
    var.push_back(*(priorData + i * 8 + 5));
    var.push_back(*(priorData + i * 8 + 6));
    var.push_back(*(priorData + i * 8 + 7));
    varVec[outOffset + i] = var;
  }
}

void getBBoxFromLabelData(const real* labelData,
                          const size_t numBBoxes,
                          vector<NormalizedBBox>& bboxVec) {
  size_t outOffset = bboxVec.size();
  bboxVec.resize(bboxVec.size() + numBBoxes);
  for (size_t i = 0; i < numBBoxes; ++i) {
    NormalizedBBox bbox;
    bbox.xMin = *(labelData + i * 6 + 1);
    bbox.yMin = *(labelData + i * 6 + 2);
    bbox.xMax = *(labelData + i * 6 + 3);
    bbox.yMax = *(labelData + i * 6 + 4);
    bboxVec[outOffset + i] = bbox;
  }
}

void matchBBox(const vector<NormalizedBBox>& priorBBoxes,
               const vector<NormalizedBBox>& gtBBoxes,
               real overlapThreshold,
               vector<int>* matchIndices,
               vector<real>* matchOverlaps) {
  map<size_t, map<size_t, real>> overlaps;
  size_t numPriors = priorBBoxes.size();
  size_t numGTs = gtBBoxes.size();

  matchIndices->clear();
  matchIndices->resize(numPriors, -1);
  matchOverlaps->clear();
  matchOverlaps->resize(numPriors, 0.0);

  // Store the positive overlap between predictions and ground truth
  for (size_t i = 0; i < numPriors; ++i) {
    for (size_t j = 0; j < numGTs; ++j) {
      real overlap = jaccardOverlap(priorBBoxes[i], gtBBoxes[j]);
      if (overlap > 1e-6) {
        (*matchOverlaps)[i] = std::max((*matchOverlaps)[i], overlap);
        overlaps[i][j] = overlap;
      }
    }
  }
  // Bipartite matching
  vector<int> gtPool;
  for (size_t i = 0; i < numGTs; ++i) {
    gtPool.push_back(i);
  }
  while (gtPool.size() > 0) {
    // Find the most overlapped gt and corresponding predictions
    int maxPriorIdx = -1;
    int maxGTIdx = -1;
    real maxOverlap = -1.0;
    for (map<size_t, map<size_t, real>>::iterator it = overlaps.begin();
         it != overlaps.end();
         ++it) {
      size_t i = it->first;
      if ((*matchIndices)[i] != -1) {
        // The prediction already has matched ground truth or is ignored
        continue;
      }
      for (size_t p = 0; p < gtPool.size(); ++p) {
        int j = gtPool[p];
        if (it->second.find(j) == it->second.end()) {
          // No overlap between the i-th prediction and j-th ground truth
          continue;
        }
        // Find the maximum overlapped pair
        if (it->second[j] > maxOverlap) {
          maxPriorIdx = (int)i;
          maxGTIdx = (int)j;
          maxOverlap = it->second[j];
        }
      }
    }
    if (maxPriorIdx == -1) {
      break;
    } else {
      (*matchIndices)[maxPriorIdx] = maxGTIdx;
      (*matchOverlaps)[maxPriorIdx] = maxOverlap;
      gtPool.erase(std::find(gtPool.begin(), gtPool.end(), maxGTIdx));
    }
  }

  // Get most overlaped for the rest prediction bboxes
  for (map<size_t, map<size_t, real>>::iterator it = overlaps.begin();
       it != overlaps.end();
       ++it) {
    size_t i = it->first;
    if ((*matchIndices)[i] != -1) {
      // The prediction already has matched ground truth or is ignored
      continue;
    }
    int maxGTIdx = -1;
    real maxOverlap = -1;
    for (size_t j = 0; j < numGTs; ++j) {
      if (it->second.find(j) == it->second.end()) {
        // No overlap between the i-th prediction and j-th ground truth
        continue;
      }
      // Find the maximum overlapped pair
      real overlap = it->second[j];
      if (overlap > maxOverlap && overlap >= overlapThreshold) {
        maxGTIdx = j;
        maxOverlap = overlap;
      }
    }
    if (maxGTIdx != -1) {
      (*matchIndices)[i] = maxGTIdx;
      (*matchOverlaps)[i] = maxOverlap;
    }
  }
}

pair<size_t, size_t> generateMatchIndices(
    const MatrixPtr priorValue,
    const size_t numPriorBBoxes,
    const MatrixPtr gtValue,
    const int* gtStartPosPtr,
    const size_t seqNum,
    const vector<vector<real>>& maxConfScore,
    const size_t batchSize,
    const real overlapThreshold,
    const real negOverlapThreshold,
    const size_t negPosRatio,
    vector<vector<int>>* matchIndicesVecPtr,
    vector<vector<int>>* negIndicesVecPtr) {
  vector<NormalizedBBox> priorBBoxes;  // share same prior bboxes
  getBBoxFromPriorData(priorValue->getData(), numPriorBBoxes, priorBBoxes);
  size_t totalPos = 0;
  size_t totalNeg = 0;
  for (size_t n = 0; n < batchSize; ++n) {
    vector<int> matchIndices;
    vector<int> negIndices;
    vector<real> matchOverlaps;
    matchIndices.resize(numPriorBBoxes, -1);
    matchOverlaps.resize(numPriorBBoxes, 0.0);
    size_t numGTBBoxes = 0;
    if (n < seqNum) numGTBBoxes = gtStartPosPtr[n + 1] - gtStartPosPtr[n];
    if (!numGTBBoxes) {
      matchIndicesVecPtr->push_back(matchIndices);
      negIndicesVecPtr->push_back(negIndices);
      continue;
    }
    vector<NormalizedBBox> gtBBoxes;
    getBBoxFromLabelData(
        gtValue->getData() + gtStartPosPtr[n] * 6, numGTBBoxes, gtBBoxes);

    matchBBox(
        priorBBoxes, gtBBoxes, overlapThreshold, &matchIndices, &matchOverlaps);

    size_t numPos = 0;
    size_t numNeg = 0;
    for (size_t i = 0; i < matchIndices.size(); ++i)
      if (matchIndices[i] != -1) ++numPos;
    totalPos += numPos;
    vector<pair<real, size_t>> scoresIndices;
    for (size_t i = 0; i < matchIndices.size(); ++i)
      if (matchIndices[i] == -1 && matchOverlaps[i] < negOverlapThreshold) {
        scoresIndices.push_back(std::make_pair(maxConfScore[n][i], i));
        ++numNeg;
      }
    numNeg = std::min(static_cast<size_t>(numPos * negPosRatio), numNeg);
    std::sort(scoresIndices.begin(),
              scoresIndices.end(),
              sortScorePairDescend<size_t>);
    for (size_t i = 0; i < numNeg; ++i)
      negIndices.push_back(scoresIndices[i].second);
    totalNeg += numNeg;
    matchIndicesVecPtr->push_back(matchIndices);
    negIndicesVecPtr->push_back(negIndices);
  }
  return std::make_pair(totalPos, totalNeg);
}

void getMaxConfidenceScores(const real* confData,
                            const size_t batchSize,
                            const size_t numPriorBBoxes,
                            const size_t numClasses,
                            const size_t backgroundId,
                            vector<vector<real>>* maxConfScoreVecPtr) {
  maxConfScoreVecPtr->clear();
  for (size_t i = 0; i < batchSize; ++i) {
    vector<real> maxConfScore;
    for (size_t j = 0; j < numPriorBBoxes; ++j) {
      int offset = j * numClasses;
      real maxVal = -FLT_MAX;
      real maxPosVal = -FLT_MAX;
      real maxScore = 0.0;
      for (size_t c = 0; c < numClasses; ++c) {
        maxVal = std::max<real>(confData[offset + c], maxVal);
        if (c != backgroundId)
          maxPosVal = std::max<real>(confData[offset + c], maxPosVal);
      }
      real sum = 0.0;
      for (size_t c = 0; c < numClasses; ++c)
        sum += std::exp(confData[offset + c] - maxVal);
      maxScore = std::exp(maxPosVal - maxVal) / sum;
      maxConfScore.push_back(maxScore);
    }
    confData += numPriorBBoxes * numClasses;
    maxConfScoreVecPtr->push_back(maxConfScore);
  }
}

template <typename T>
bool sortScorePairDescend(const pair<real, T>& pair1,
                          const pair<real, T>& pair2) {
  return pair1.first > pair2.first;
}

}  // namespace paddle
