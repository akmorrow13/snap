/*++

Module Name:

	TenXClusterAligner.cpp

Abstract:

	A paired-end aligner calls into a different paired-end aligner, and if
	it fails to find an alignment, aligns each of the reads singly.  This handles
	chimeric reads that would otherwise be unalignable.

Authors:

	Bill Bolosky, June, 2013

Environment:

	User mode service.

Revision History:

--*/


#include "stdafx.h"
#include "TenXClusterAligner.h"
#include "mapq.h"
#include "directions.h"
#include "BigAlloc.h"
#include <cstdlib>
#include "Util.h"

using namespace std;

#ifdef TRACE_PAIRED_ALIGNER
#define TRACE printf
#else
#define TRACE(...) {}
#endif

TenXClusterAligner::TenXClusterAligner(
	GenomeIndex			*index_,
	unsigned			maxReadSize,
	unsigned			maxHits,
	unsigned			maxK,
	unsigned			maxSeedsFromCommandLine,
	double				seedCoverage,
	unsigned			minWeightToCheck,
	bool				forceSpacing_,
	unsigned			extraSearchDepth,
	bool				noUkkonen,
	bool				noOrderedEvaluation,
	bool				noTruncation,
	bool				ignoreAlignmentAdjustmentsForOm,
	TenXProgressTracker	*progressTracker_,
	unsigned			maxBarcodeSize_,
	unsigned			minPairsPerCluster_,
	_uint64				maxClusterSpan_,
	unsigned			minReadLength_,
	int					maxSecondaryAlignmentsPerContig,
	BigAllocator		*allocator)
	: progressTracker(progressTracker_), maxBarcodeSize(maxBarcodeSize_), minPairsPerCluster(minPairsPerCluster_), maxClusterSpan(maxClusterSpan_), forceSpacing(forceSpacing_), index(index_), minReadLength(minReadLength_)
{
	// Create single-end aligners.
	singleAligner = new (allocator) BaseAligner(index, maxHits, maxK, maxReadSize,
		maxSeedsFromCommandLine, seedCoverage, minWeightToCheck, extraSearchDepth, noUkkonen, noOrderedEvaluation, noTruncation, ignoreAlignmentAdjustmentsForOm, maxSecondaryAlignmentsPerContig, &lv, &reverseLV, NULL, allocator);
	for (unsigned i = 0; i < maxBarcodeSize; i++)
		progressTracker[i].aligner->setLandauVishkin(&lv, &reverseLV);

	singleSecondary[0] = singleSecondary[1] = NULL;
}

size_t
TenXClusterAligner::getBigAllocatorReservation(
	GenomeIndex		*index,
	unsigned		maxReadSize,
	unsigned		maxHits,
	unsigned		seedLen,
	unsigned		maxSeedsFromCommandLine,
	double			seedCoverage,
	unsigned		maxEditDistanceToConsider,
	unsigned		maxExtraSearchDepth,
	unsigned		maxCandidatePoolSize,
	int				maxSecondaryAlignmentsPerContig)
{
	return BaseAligner::getBigAllocatorReservation(index, false, maxHits, maxReadSize, seedLen, maxSeedsFromCommandLine, seedCoverage, maxSecondaryAlignmentsPerContig, maxExtraSearchDepth) + sizeof(TenXClusterAligner) + sizeof(_uint64);
}


TenXClusterAligner::~TenXClusterAligner()
{
	singleAligner->~BaseAligner();
}

#ifdef _DEBUG
extern bool _DumpAlignments;
#endif // _DEBUG


bool TenXClusterAligner::align_first_stage(
	unsigned				barcodeSize
)
{
	bool barcodeFinished = true;
	for (int pairIdx = 0; pairIdx < barcodeSize; pairIdx++) {
		if (progressTracker[pairIdx].pairNotDone) {
			progressTracker[pairIdx].result[0].status[0] = progressTracker[pairIdx].result[0].status[1] = NotFound;

			Read *read0 = progressTracker[pairIdx].pairedReads;
			Read *read1 = progressTracker[pairIdx].pairedReads + 1;

			if (read0->getDataLength() < minReadLength && read1->getDataLength() < minReadLength) {
				TRACE("Reads are both too short -- returning");
				// Update primary result
				for (int whichRead = 0; whichRead < NUM_READS_PER_PAIR; whichRead++) {
					progressTracker[pairIdx].result[0].location[whichRead] = 0;
					progressTracker[pairIdx].result[0].mapq[whichRead] = 0;
					progressTracker[pairIdx].result[0].score[whichRead] = 0;
					progressTracker[pairIdx].result[0].status[whichRead] = NotFound;
				}
				progressTracker[pairIdx].result[0].alignedAsPair = false;
				progressTracker[pairIdx].result[0].fromAlignTogether = false;
				progressTracker[pairIdx].result[0].nanosInAlignTogether = 0;
				progressTracker[pairIdx].result[0].nLVCalls = 0;
				progressTracker[pairIdx].result[0].nSmallHits = 0;

				progressTracker[pairIdx].pairNotDone = false;
				progressTracker[pairIdx].singleNotDone = false;
				continue;//return true;
			}

			//At least one read of the pair is worthy of further examination
			barcodeFinished = false;

			if (read0->getDataLength() >= minReadLength && read1->getDataLength() >= minReadLength) {
				//
				// Let the LVs use the cache that we built up.
				//
				progressTracker[pairIdx].pairNotDone = !progressTracker[pairIdx].aligner->align_phase_1(read0, read1, progressTracker[pairIdx].popularSeedsSkipped);
				
				// Initialize for phase_2 if the alginer if not stopped
				if (progressTracker[pairIdx].pairNotDone) {
					progressTracker[pairIdx].pairNotDone = progressTracker[pairIdx].aligner->align_phase_2_init();
					progressTracker[pairIdx].nextLoci = *(progressTracker[pairIdx].aligner->align_phase_2_get_loci() );
					progressTracker[pairIdx].next = NULL;
				}
			}
		}
	}

	// TenX code. Initialize and sort all trackers
	// qsort(progressTracker, maxBarcodeSize, sizeof(TenXProgressTracker), TenXProgressTracker::compare);

	GenomeLocation clusterTargetLoc = GenomeLocation(0000000000); //**** This is just for testing. Will be removed.

	for (int pairIdx = 0; pairIdx < barcodeSize; pairIdx++) {
		if (progressTracker[pairIdx].pairNotDone) {
			//fprintf(stderr, "lastLoci: %lld\n", progressTracker[pairIdx].nextLoci.location);
			progressTracker[pairIdx].aligner->align_phase_2_to_target_loc(clusterTargetLoc, NULL);
		}
		//else
			//fprintf(stderr, "lastLoci: NULL\n");
	}
	



/*
				if (!terminateAfterPhase1) {
					progressTracker[pairIdx].aligner->align_phase_2();
				}
			}
		}
	}
*/
	return barcodeFinished;

}


bool TenXClusterAligner::align_second_stage(
	unsigned				barcodeSize,
	int						maxEditDistanceForSecondaryResults,
	_int64					maxSecondaryAlignmentsToReturn
)
{
	bool barcodeFinished = true;
	for (int pairIdx = 0; pairIdx < barcodeSize; pairIdx++) {
		if (progressTracker[pairIdx].pairNotDone) {// && progressTracker[pairIdx].notDone) {
			Read *read0 = progressTracker[pairIdx].pairedReads;
			Read *read1 = progressTracker[pairIdx].pairedReads + 1;

			progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead = 0;
			progressTracker[pairIdx].nSingleEndSecondaryResultsForSecondRead = 0;

			unsigned bestPairScore = 65536;
			GenomeLocation bestResultGenomeLocation[NUM_READS_PER_PAIR];
			Direction bestResultDirection[NUM_READS_PER_PAIR];
			double probabilityOfAllPairs = 0;
			unsigned bestResultScore[NUM_READS_PER_PAIR];
			double probabilityOfBestPair = 0;

			bool secondaryBufferOverflow = progressTracker[pairIdx].aligner->align_phase_3(maxEditDistanceForSecondaryResults, progressTracker[pairIdx].secondaryResultBufferSize, &progressTracker[pairIdx].nSecondaryResults, &progressTracker[pairIdx].result[1], maxSecondaryAlignmentsToReturn, bestPairScore, bestResultGenomeLocation, bestResultDirection, probabilityOfAllPairs, bestResultScore, progressTracker[pairIdx].popularSeedsSkipped, probabilityOfBestPair);

			if (secondaryBufferOverflow) {
				progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead = progressTracker[pairIdx].nSingleEndSecondaryResultsForSecondRead = 0;
				progressTracker[pairIdx].nSecondaryResults = progressTracker[pairIdx].secondaryResultBufferSize + 1; // So the caller knows it's the paired secondary buffer that overflowed
				barcodeFinished = false;
				continue;//return false;
			}
			else {
				progressTracker[pairIdx].aligner->align_phase_4(read0, read1, &progressTracker[pairIdx].result[0], maxEditDistanceForSecondaryResults, &progressTracker[pairIdx].nSecondaryResults, &progressTracker[pairIdx].result[1], maxSecondaryAlignmentsToReturn, progressTracker[pairIdx].popularSeedsSkipped, bestPairScore, bestResultGenomeLocation, bestResultDirection, probabilityOfAllPairs, bestResultScore, probabilityOfBestPair);
			}

			/* timing no longer makes sence
			_int64 start = timeInNanos();
			_int64 end = timeInNanos();
			result[pairIdx]->nanosInAlignTogether = end - start;
			*/

			progressTracker[pairIdx].result[0].nanosInAlignTogether = 0; //timing no longer makes sence
			progressTracker[pairIdx].result[0].fromAlignTogether = true;
			progressTracker[pairIdx].result[0].alignedAsPair = true;

			if (forceSpacing) {
				if (progressTracker[pairIdx].result[0].status[0] == NotFound) {
					progressTracker[pairIdx].result[0].fromAlignTogether = false;
				}
				else {
					_ASSERT(result[pairIdx]->status[1] != NotFound); // If one's not found, so is the other
				}
				progressTracker[pairIdx].pairNotDone = false;
				progressTracker[pairIdx].singleNotDone = false;
				continue;//return true;
			}

			if (progressTracker[pairIdx].result[0].status[0] != NotFound && progressTracker[pairIdx].result[0].status[1] != NotFound) {
				//
				// Not a chimeric read.
				//
				progressTracker[pairIdx].pairNotDone = false;
				progressTracker[pairIdx].singleNotDone = false;
				continue;//return true;
			}
			//paired analysis is done anyways
			progressTracker[pairIdx].pairNotDone = false;
		}
	}
	return barcodeFinished;
}


bool TenXClusterAligner::align_third_stage(
	unsigned barcodeSize,
	int maxEditDistanceForSecondaryResults,
	_int64 maxSecondaryAlignmentsToReturn
)
{
	bool barcodeFinished = true;
	for (unsigned pairIdx = 0; pairIdx < barcodeSize; pairIdx++) {
		if (progressTracker[pairIdx].singleNotDone) {// && progressTracker[pairIdx].notDone) {
			Read *read0 = progressTracker[pairIdx].pairedReads;
			Read *read1 = progressTracker[pairIdx].pairedReads + 1;

			Read *read[NUM_READS_PER_PAIR] = { read0, read1 };
			_int64 *resultCount[2] = { &progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead, &progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead };

			bool noOverflow = true;
			for (int r = 0; r < NUM_READS_PER_PAIR; r++) {
				SingleAlignmentResult singleResult;
				_int64 singleEndSecondaryResultsThisTime = 0;

				if (read[r]->getDataLength() < minReadLength) {
					progressTracker[pairIdx].result[0].status[r] = NotFound;
					progressTracker[pairIdx].result[0].mapq[r] = 0;
					progressTracker[pairIdx].result[0].direction[r] = FORWARD;
					progressTracker[pairIdx].result[0].location[r] = 0;
					progressTracker[pairIdx].result[0].score[r] = 0;
				}
				else {
					// We're using *nSingleEndSecondaryResultsForFirstRead because it's either 0 or what all we've seen (i.e., we know NUM_READS_PER_PAIR is 2)
					bool fitInSecondaryBuffer = //true;
						singleAligner->AlignRead(read[r], &singleResult, maxEditDistanceForSecondaryResults,
							progressTracker[pairIdx].singleSecondaryBufferSize - progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead, &singleEndSecondaryResultsThisTime,
							maxSecondaryAlignmentsToReturn, progressTracker[pairIdx].singleEndSecondaryResults + progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead);

					if (!fitInSecondaryBuffer) {
						progressTracker[pairIdx].nSecondaryResults = 0;
						progressTracker[pairIdx].nSingleEndSecondaryResultsForFirstRead = progressTracker[pairIdx].singleSecondaryBufferSize + 1;
						progressTracker[pairIdx].nSingleEndSecondaryResultsForSecondRead = 0;
						barcodeFinished = false;
						noOverflow = false;
						break;//return false;
					}

					*(resultCount[r]) = singleEndSecondaryResultsThisTime;

					progressTracker[pairIdx].result[0].status[r] = singleResult.status;
					progressTracker[pairIdx].result[0].mapq[r] = singleResult.mapq / 3;   // Heavy quality penalty for chimeric reads
					progressTracker[pairIdx].result[0].direction[r] = singleResult.direction;
					progressTracker[pairIdx].result[0].location[r] = singleResult.location;
					progressTracker[pairIdx].result[0].score[r] = singleResult.score;
					progressTracker[pairIdx].result[0].scorePriorToClipping[r] = singleResult.scorePriorToClipping;
				}
			}

			// This pair is done processing, only if both directions has no overflow.
			if (noOverflow) {
				progressTracker[pairIdx].singleNotDone = false;
				progressTracker[pairIdx].result[0].fromAlignTogether = false;
				progressTracker[pairIdx].result[0].alignedAsPair = false;
			}

#ifdef _DEBUG
			if (_DumpAlignments) {
				printf("TenXClusterAligner: (%u, %u) score (%d, %d), MAPQ (%d, %d)\n\n\n", progressTracker[pairIdx].result[0].location[0].location, result[pairIdx]->location[1].location,
					progressTracker[pairIdx].result[0].score[0], result[pairIdx]->score[1], result[pairIdx]->mapq[0], result[pairIdx]->mapq[1]);
			}
#endif // _DEBUG

		}
	}
	return barcodeFinished;
}



bool TenXClusterAligner::align(
	Read					**pairedReads,
	unsigned				barcodeSize,
	PairedAlignmentResult	**result,
	int						maxEditDistanceForSecondaryResults,
	_int64					*secondaryResultBufferSize,
	_int64					*nSecondaryResults,
	_int64					*singleSecondaryBufferSize,
	_int64					maxSecondaryAlignmentsToReturn,
	_int64					*nSingleEndSecondaryResults,
	SingleAlignmentResult	**singleEndSecondaryResults,    // Single-end secondary alignments for when the paired-end alignment didn't work properly
	unsigned				*popularSeedsSkipped
)
{
	if (align_first_stage(barcodeSize))
		return true;
	if (!align_second_stage(barcodeSize, maxEditDistanceForSecondaryResults, maxSecondaryAlignmentsToReturn))
		return false;
	if (align_third_stage(barcodeSize, maxEditDistanceForSecondaryResults, maxSecondaryAlignmentsToReturn))
		return true;
	return false;
}
