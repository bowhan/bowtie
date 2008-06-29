#ifdef EBWT_SEARCH_MAIN

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cassert>
#include <seqan/find.h>
#include <getopt.h>
#include "alphabet.h"
#include "assert_helpers.h"
#include "endian.h"
#include "packed_io.h"
#include "ebwt.h"
#include "params.h"
#include "sequence_io.h"
#include "tokenize.h"
#include "hit.h"
#include "pat.h"
#include "inexact_extend.h"

using namespace std;
using namespace seqan;

static int verbose				= 0;
static int allowed_diffs		= -1;
static int kmer					= -1;
static int sanityCheck			= 0;
static int format				= FASTA;
static string origString		= "";
static int revcomp				= 0; // search for reverse complements?
static int seed					= 0; // srandom() seed
static int timing				= 0; // whether to report basic timing data
static bool oneHit				= true; // for multihits, report just one
static int ipause				= 0; // pause before maching?
static int binOut				= 0; // write hits in binary
static int qUpto				= -1; // max # of queries to read
static int skipSearch			= 0; // abort before searching
static int qSameLen				= 0; // abort before searching
static int trim5				= 0; // amount to trim from 5' end
static int trim3				= 0; // amount to trim from 3' end
static int printStats			= 0; // whether to print statistics
static int reportOpps			= 0; // whether to report # of other mappings
static int offRate				= -1; // keep default offRate
static int mismatches			= 0; // allow 0 mismatches by default
static char *patDumpfile		= NULL; // filename to dump patterns to

static const char *short_options = "fqbmlcu:rvsat3:5:1o:k:d:";

#define ORIG_ARG 256
#define ARG_SEED 257
#define ARG_DUMP_PATS 258

static struct option long_options[] = {
	/* These options set a flag. */
	{"verbose", no_argument, 0, 'v'},
	{"sanity",  no_argument, 0, 's'},
	{"1mismatch",  no_argument, 0, '1'},
	{"pause",   no_argument, &ipause, 1},
	{"orig",    required_argument, 0, ORIG_ARG},
	{"allHits", no_argument, 0, 'a'},
	{"binOut",  no_argument, 0, 'b'},
	{"time",    no_argument, 0, 't'},
	{"trim3",   required_argument, 0, '3'},
	{"trim5",   required_argument, 0, '5'},
	{"seed",    required_argument, 0, ARG_SEED},
	{"qUpto",   required_argument, 0, 'u'},
	{"offRate",   required_argument, 0, 'o'},
	{"skipSearch", no_argument, &skipSearch, 1},
	{"qSameLen",   no_argument, &qSameLen, 1},
	{"stats",      no_argument, &printStats, 1},
	{"reportOpps", no_argument, &reportOpps, 1},
	{"dumpPats",   required_argument, 0, ARG_DUMP_PATS},
	{"revcomp", no_argument, 0, 'r'},
	{"kmer", required_argument, 0, 'k'},
	{"3prime-diffs", required_argument, 0, 'd'},
	{0, 0, 0, 0}
};

/**
 * Print a detailed usage message to the provided output stream.
 */
static void printUsage(ostream& out) {
	out << "Usage: ebwt_search [options]* <ebwt_infile_base> <query_in> [<hit_outfile>]" << endl
	    << "  ebwt_infile_base   ebwt filename minus trailing .1.ebwt/.2.ebwt" << endl
	    << "  query_in           comma-separated list of files containing query reads" << endl
	    << "                     (or the sequences themselves, if -c is specified)" << endl
	    << "  hit_outfile        file to write hits to" << endl
	    << "Options:" << endl
	    << "  -f                 query input files are (multi-)FASTA .fa/.mfa (default)" << endl
	    << "  -q                 query input files are FASTQ .fq" << endl
	    << "  -m                 query input files are Maq .bfq" << endl
	    << "  -l                 query input files are Solexa _seq.txt" << endl
	    << "  -c                 query sequences given on command line (as <query_in>)" << endl
	    << "  -o/--offRate <int> override offRate of Ebwt (must be <= value in index)" << endl
	    << "  -1/--1mismatch     allow 1 mismatch (requires both fw and bw Ebwts)" << endl
	    << "  -5/--trim5 <int>   # of bases to trim from 5' (right) end of queries" << endl
	    << "  -3/--trim3 <int>   # of bases to trim from 3' (left) end of queries" << endl
	    << "  -u/--qUpto <int>   stop after <int> queries (counting reverse complements)" << endl
	    << "  -r/--revcomp       also search for rev. comp. of each query (default: off)" << endl
		<< "  -k/--kmer [int]    match on the 5' #-mer and then extend hits with a more sensitive alignment (default: 22bp)" << endl
		<< "  -d/--3prime-diffs  # of differences in the 3' end, when used with -k above (default: 4)" << endl
	    << "  -b/--binOut        write hits in binary format (must specify <hit_outfile>)" << endl
	    << "  -t/--time          print basic timing statistics" << endl
	    << "  -v/--verbose       verbose output (for debugging)" << endl
	    //<< "  -s/--sanity        enable sanity checks (increases runtime and mem usage!)" << endl
	    << "  -a/--allHits       if query has >1 hit, give all hits (default: 1 random hit)" << endl
	    //<< "  --orig <str>       specify original string (for sanity-checking)" << endl
	    //<< "  --qSameLen         die with error if queries don't all have the same length" << endl
	    << "  --stats            write statistics after hits" << endl
	    << "  --reportOpps       report # of other potential mapping targets for each hit" << endl
	    //<< "  --dumpPats <file>  dump all patterns read to a file" << endl
	    << "  --seed <int>       seed for random number generator" << endl;
}

/**
 * Parse an int out of optarg and enforce that it be at least 'lower';
 * if it is less than 'lower', than output the given error message and
 * exit with an error and a usage message.
 */
static int parseInt(int lower, const char *errmsg) {
	long l;
	char *endPtr= NULL;
	l = strtol(optarg, &endPtr, 10);
	if (endPtr != NULL) {
		if (l < lower) {
			cerr << errmsg << endl;
			printUsage(cerr);
			exit(1);
		}
		return (int32_t)l;
	}
	cerr << errmsg << endl;
	printUsage(cerr);
	exit(1);
	return -1;
}

/**
 * Read command-line arguments
 */
static void parseOptions(int argc, char **argv) {

    /* getopt_long stores the option index here. */
    int option_index = 0;
	int next_option;
	do {
		next_option = getopt_long(argc, argv, short_options, long_options, &option_index);
		switch (next_option) {
	   		case 'f': format = FASTA; break;
	   		case 'q': format = FASTQ; break;
	   		case 'm': format = BFQ; break;
	   		case 'l': format = SOLEXA; break;
	   		case 'c': format = CMDLINE; break;
	   		case '1': mismatches = 1; break;
	   		case 'r': revcomp = 1; break;
	   		case ARG_SEED:
	   			seed = parseInt(0, "--seed arg must be at least 0");
	   			break;
	   		case 'u':
	   			qUpto = parseInt(1, "-u/--qUpto arg must be at least 1");
	   			break;
	   		case '3':
	   			trim3 = parseInt(0, "-3/--trim3 arg must be at least 0");
	   			break;
	   		case '5':
	   			trim5 = parseInt(0, "-5/--trim5 arg must be at least 0");
	   			break;
	   		case 'o':
	   			offRate = parseInt(1, "-o/--offRate arg must be at least 1");
	   			break;
	   		case 'a': oneHit = false; break;
	   		case 'v': verbose = true; break;
	   		case 's': sanityCheck = true; break;
	   		case 't': timing = true; break;
	   		case 'b': binOut = true; break;
			case 'k': 
				if (optarg != NULL)
					kmer = parseInt(1, "-k/--kmer must be at least 1");
				else
					kmer = 22;
				break;
			case 'd': 
				if (optarg)
					allowed_diffs = parseInt(0, "-d/--3prime-diffs must be at least 0");
				else
					allowed_diffs = 4;
				break;
	   		case ARG_DUMP_PATS:
	   			patDumpfile = optarg;
	   			break;
	   		case ORIG_ARG:
   				if(optarg == NULL || strlen(optarg) == 0) {
   					cerr << "--orig arg must be followed by a string" << endl;
   					printUsage(cerr);
   					exit(1);
   				}
   				origString = optarg;
	   			break;

			case -1: break; /* Done with options. */
			case 0:
				if (long_options[option_index].flag != 0)
					break;	
			default: 
				cerr << "Unknown option: " << (char)next_option << endl;
				printUsage(cerr);
				exit(1);
		}
	} while(next_option != -1);
}

static char *argv0 = NULL;

/**
 * Search through a single (forward) Ebwt index for exact query hits.
 * Ebwt (ebwt) is already loaded into memory.
 */
template<typename TStr>
static void exactSearch(PatternSource<TStr>& patsrc,
                        HitSink& sink,
                        EbwtSearchStats<TStr>& stats,
                        EbwtSearchParams<TStr>& params,
                        Ebwt<TStr>& ebwt,
                        vector<TStr>& os)
{
	uint32_t patid = 0;
	uint64_t lastHits = 0llu;
	uint32_t lastLen = 0;
	assert(patsrc.hasMorePatterns());
    while(patsrc.hasMorePatterns() && patid < (uint32_t)qUpto) {
    	params.setFw(!revcomp || !patsrc.nextIsReverseComplement());
    	params.setPatId(patid++);
    	assert(!revcomp || (params.patId() & 1) == 0 || !params.fw());
    	assert(!revcomp || (params.patId() & 1) == 1 ||  params.fw());
    	TStr pat = patsrc.nextPattern();
    	assert(!empty(pat));
    	if(lastLen == 0) lastLen = length(pat);
    	if(qSameLen && length(pat) != lastLen) {
    		throw runtime_error("All reads must be the same length");
    	}
    	EbwtSearchState<TStr> s(ebwt, pat, params, seed);
    	params.stats().incRead(s, pat);
	    ebwt.search(s, params);
	    // If the forward direction matched exactly, ignore the
	    // reverse complement
	    if(oneHit && revcomp && sink.numHits() > lastHits) {
	    	lastHits = sink.numHits();
	    	if(params.fw()) {
	    		assert(patsrc.nextIsReverseComplement());
	    		assert(patsrc.hasMorePatterns());
	    		// Ignore this pattern (the reverse complement of
	    		// the one we just matched)
		    	const TStr& pat2 = patsrc.nextPattern();
		    	assert(!empty(pat2));
		    	patid++;
		    	if(qSameLen && length(pat2) != lastLen) {
		    		throw runtime_error("All reads must be the same length");
		    	}
		    	params.setFw(false);
		    	params.stats().incRead(s, pat2);
	    		assert(!patsrc.nextIsReverseComplement());
	    	}
	    }
    	// Optionally sanity-check results by confirming with a
    	// different matcher that the pattern occurs in exactly
    	// those locations reported.  
    	if(sanityCheck && !oneHit && !os.empty()) {
    	    vector<Hit>& results = sink.retainedHits();
		    vector<U32Pair> results2;
		    results2.reserve(256);
		    for(unsigned int i = 0; i < os.size(); i++) {
	    		// Forward
	    		Finder<TStr> finder(os[i]);
	    		Pattern<TStr, Horspool> pattern(pat);
	    		while (find(finder, pattern)) {
	    			results2.push_back(make_pair(i, position(finder)));
	    		}
		    }
    		sort(results.begin(), results.end());
    		if(oneHit) {
	    		assert_leq(results.size(), results2.size());
	    		for(int i = 0; i < (int)results.size(); i++) {
	    			bool foundMatch = false;
		    		for(int j = i; j < (int)results2.size(); j++) {
		    			if(results[i].h.first == results2[j].first &&
		    			   results[i].h.second == results2[j].second)
		    			{
		    				foundMatch = true;
		    				break;
		    			}
		    		}
		    		assert(foundMatch);
	    		}
    		} else {
	    		assert_eq(results.size(), results2.size());
	    		for(int i = 0; i < (int)results.size(); i++) {
	    			assert_eq(results[i].h.first, results2[i].first);
	    			assert_eq(results[i].h.second, results2[i].second);
	    		}
    		}
    		if(verbose) {
    			cout << "Passed orig/result sanity-check ("
    			     << results2.size() << " results checked) for pattern "
    			     << patid << endl;
    		}
    		sink.clearRetainedHits();
    	}
    }
}



/**
 * Search through a single (forward) Ebwt index for exact query hits in the 
 * 5' end of each read, and then extend that hit by shift-and to allow for 3'
 * mismatches.
 *
 * Ebwt (ebwt) is already loaded into memory.
 */
template<typename TStr>
static void exactSearchWithExtension(vector<String<Dna, Packed<> > >& packed_texts,
									 PatternSource<TStr>& patsrc,
									 HitSink& sink,
									 EbwtSearchStats<TStr>& stats,
									 EbwtSearchParams<TStr>& params,
									 Ebwt<TStr>& ebwt,
									 vector<TStr>& os)
{
	uint32_t patid = 0;
	uint64_t lastHits= 0llu;
	uint32_t lastLen = 0;
	assert(patsrc.hasMorePatterns());
	
	if (allowed_diffs == -1)
		allowed_diffs = default_allowed_diffs;
	
	ExactSearchWithLowQualityThreePrime<TStr> extend_policy(packed_texts, 
															false, 
															kmer,/* global, override */ 
															allowed_diffs /*global, override*/);
	
    while(patsrc.hasMorePatterns() && patid < (uint32_t)qUpto) {
    	params.setFw(!revcomp || !patsrc.nextIsReverseComplement());
    	params.setPatId(patid++);
    	assert(!revcomp || (params.patId() & 1) == 0 || !params.fw());
    	assert(!revcomp || (params.patId() & 1) == 1 ||  params.fw());
    	TStr pat = patsrc.nextPattern();
    	assert(!empty(pat));
    	if(lastLen == 0) lastLen = length(pat);
    	if(qSameLen && length(pat) != lastLen) {
    		throw runtime_error("All reads must be the same length");
    	}
		
    	// FIXME:
    	//params.stats().incRead(s, pat);
	    extend_policy.search(ebwt, stats, params, pat, sink);
		
	    // If the forward direction matched exactly, ignore the
	    // reverse complement
	    if(oneHit && revcomp && sink.numHits() > lastHits) {
	    	lastHits = sink.numHits();
	    	if(params.fw()) {
	    		assert(patsrc.nextIsReverseComplement());
	    		assert(patsrc.hasMorePatterns());
	    		// Ignore this pattern (the reverse complement of
	    		// the one we just matched)
		    	const TStr& pat2 = patsrc.nextPattern();
		    	assert(!empty(pat2));
		    	patid++;
		    	if(qSameLen && length(pat2) != lastLen) {
		    		throw runtime_error("All reads must be the same length");
		    	}
		    	params.setFw(false);
				
				//FIXME:
		    	//params.stats().incRead(s, pat2);
	    		assert(!patsrc.nextIsReverseComplement());
	    	}
	    }
    }
}


/**
 * Given a pattern, a list of reference texts, and some other state,
 * find all hits for that pattern in all texts using a naive seed- 
 * and-extend algorithm where seeds are found using Horspool.
 */
template<typename TStr1, typename TStr2>
static bool findSanityHits(const TStr1& pat,
                           uint32_t patid,
                           bool fw,
                           vector<TStr2>& os,
                           vector<Hit>& sanityHits,
                           bool allowExact,
                           bool transpose)
{
	typedef TStr1 TStr;
	typedef typename Value<TStr>::Type TVal;
    uint32_t plen = length(pat);
	TStr half;
	reserve(half, plen);
	uint32_t bump = 0;
	if(!transpose) bump = 1;
	// Grab the unrevisitable region of pat
	for(size_t i = ((plen+bump)>>1); i < plen; i++) {
		appendValue(half, (TVal)pat[i]);
	}
    uint32_t hlen = length(half); // length of seed half
    assert_leq(hlen, plen);
    uint32_t ohlen = plen - hlen; // length of other half
    assert_leq(ohlen, plen);
	Pattern<TStr, Horspool> pattern(half);
	for(size_t i = 0; i < os.size(); i++) {
		TStr2 o = os[i];
		if(transpose) {
			for(size_t j = 0; j < length(o)>>1; j++) {
				TVal tmp = o[j];
				o[j] = o[length(o)-j-1];
				o[length(o)-j-1] = tmp;
			}
		}
		Finder<TStr> finder(o);
		while (find(finder, pattern)) {
			uint32_t pos = position(finder);
			uint32_t diffs = 0;
			if(pos >= ohlen) {
				// Extend, counting mismatches
				for(uint32_t j = 0; j < ohlen && diffs <= 1; j++) {
					if(o[pos-j-1] != pat[ohlen-j-1]) {
						diffs++;
					}
				}
			}
			// If the extend yielded 1 or fewer hits, keep it
			if((diffs == 0 && allowExact) || diffs == 1) {
				uint32_t off = pos - ohlen;
				if(transpose) {
					off = length(o) - off;
					off -= length(pat);
				}
				// A hit followed by a transpose can sometimes fall
				// off the beginning of the text
				if(off < (0xffffffff - length(pat))) {
					sanityHits.push_back(Hit(make_pair(i, off), patid, fw, diffs));
				}
			}
		}
	}
	return true;
}

/**
 * Assert that the sanityHits array has been exhausted, presumably
 * after having been reconciled against actual hits with
 * reconcileHits().  Only used in allHits mode.
 */
template<typename TStr>
static bool checkSanityExhausted(const TStr& pat,
                                 uint32_t patid,
                                 bool fw,
                                 vector<Hit>& sanityHits,
                                 bool transpose)
{
    // If caller specified mustExhaust, then we additionally check
    // whether every sanityHit has now been matched up with some Ebwt
    // hit.  If not, that means that Ebwt may have missed a hit, so
    // we assert.
    size_t unfoundHits = 0;
	for(size_t j = 0; j < sanityHits.size(); j++) {
		uint32_t patid = sanityHits[j].pat;
		bool fw = sanityHits[j].fw;
		cout << "Did not find sanity hit: "
		     << (patid>>revcomp) << (fw? "+":"-")
		     << ":<" << sanityHits[j].h.first << ","
		     << sanityHits[j].h.second << ","
		     << sanityHits[j].mms << ">" << endl;
		cout << "  transpose: " << transpose << endl;
		unfoundHits++;
	}
	assert_eq(0, unfoundHits); // Ebwt missed a true hit?
	return true;
}

/**
 * Assert that every hit in the hits array also occurs in the
 * sanityHits array.
 */
template<typename TStr1, typename TStr2>
static bool reconcileHits(const TStr1& pat,
                          uint32_t patid,
                          bool fw,
                          vector<TStr2>& os,
                          vector<Hit>& hits,
                          vector<Hit>& sanityHits,
                          bool allowExact,
                          bool transpose)
{
	typedef TStr1 TStr;
	typedef typename Value<TStr>::Type TVal;
    // Sanity-check each result by checking whether it occurs
	// in the sanityHits array-of-vectors
    for(size_t i = 0; i < hits.size(); i++) {
    	const Hit& h = hits[i];
    	vector<Hit>::iterator itr;
    	bool found = false;
    	// Scan through the sanityHits vector corresponding to
    	// this hit text
    	for(itr = sanityHits.begin(); itr != sanityHits.end(); itr++) {
    		// If offset into hit text matches
			assert_gt(sanityHits.size(), 0);
    		if(h.h.first == itr->h.first && h.h.second == itr->h.second) {
    			// Assert that number of mismatches matches
    			assert_eq(h.mms, itr->mms);
    			assert_eq(h.fw, itr->fw);
    			found = true;
    			sanityHits.erase(itr); // Retire this sanity hit
    			break;
    		}
    	}
    	// Assert that the Ebwt hit was covered by a sanity-check hit
    	if(!found) {
    		cout << "Ebwt hit not found in sanity-check hits:" << endl
    		     << "  " << pat << endl;
    		cout << "  ";
    		cout << endl;
    		cout << (patid>>revcomp) << (fw? "+":"-") << ":<"
    		     << h.h.first << "," << h.h.second << "," << h.mms << ">" << endl;
    		cout << "transpose: " << transpose << endl;
    		cout << "Candidates:" << endl;
        	for(itr = sanityHits.begin(); itr != sanityHits.end(); itr++) {
        		cout << "  " << itr->h.first << " (" << itr->h.second << ")" << endl;
        	}
    	}
    	assert(found); 
    }
    return true;
}

/**
 * Search through a pair of Ebwt indexes, one for the forward direction
 * and one for the backward direction, for exact query hits and hits
 * with at most one mismatch.
 * 
 * Forward Ebwt (ebwtFw) is already loaded into memory and backward
 * Ebwt (ebwtBw) is not loaded into memory.
 */
template<typename TStr>
static void mismatchSearch(PatternSource<TStr>& patsrc,
                           HitSink& sink,
                           EbwtSearchStats<TStr>& stats,
                           EbwtSearchParams<TStr>& params,
                           Ebwt<TStr>& ebwtFw,
                           Ebwt<TStr>& ebwtBw,
                           vector<TStr>& os)
{
	typedef typename Value<TStr>::Type TVal;
	assert(ebwtFw.isInMemory());
	assert(!ebwtBw.isInMemory());
	assert(patsrc.hasMorePatterns());
    patsrc.setReverse(false); // reverse patterns
    params.setEbwtFw(true); // let search parameters reflect the forward index
	vector<Hit> sanityHits;
	uint32_t patid = 0;
	uint64_t lastHits = 0llu;
	uint32_t lastLen = 0; // for checking if all reads have same length
	String<uint8_t> doneMask;
    params.setEbwtFw(true);
	uint32_t numQs = ((qUpto == -1) ? 4 * 1024 * 1024 : qUpto);
	fill(doneMask, numQs, 0); // 4 MB, masks 32 million reads
	{
	Timer _t(cout, "Time for 1-mismatch forward search: ", timing);
    while(patsrc.hasMorePatterns() && patid < (uint32_t)qUpto) {
    	bool sfw = !revcomp || !patsrc.nextIsReverseComplement();
    	params.setFw(sfw);
    	uint32_t spatid = patid;
    	params.setPatId(spatid);
    	assert(!revcomp || (params.patId() & 1) == 0 || !params.fw());
    	assert(!revcomp || (params.patId() & 1) == 1 ||  params.fw());
    	TStr pat = patsrc.nextPattern();
    	assert(!empty(pat));
    	if(lastLen == 0) lastLen = length(pat);
    	if(qSameLen && length(pat) != lastLen) {
    		throw runtime_error("All reads must be the same length");
    	}
    	// Create state for a search on in the forward index
    	EbwtSearchState<TStr> s(ebwtFw, pat, params, seed);
    	params.stats().incRead(s, pat);
    	// Are there provisional hits?
    	if(sink.numProvisionalHits() > 0) {
    		// Shouldn't be any provisional hits unless we're doing
    		// pick-one and this is a reverse complement
    		assert(oneHit);
    		assert(!params.fw());
    		// There is a provisional inexact match for the forward
    		// orientation of this pattern, so just try exact
    		ebwtFw.search(s, params);
    	    if(sink.numHits() > lastHits) {
    	    	// Got one or more exact hits from the reverse
    	    	// complement; reject provisional hits
    	    	sink.rejectProvisionalHits();
    	    } else {
    	    	// No exact hits from reverse complement; accept
    	    	// provisional hits, thus avoiding doing an inexact
    	    	// match on the reverse complement.
        	    ASSERT_ONLY(size_t numRetained = sink.retainedHits().size());
    	    	sink.acceptProvisionalHits();
    	    	assert_eq(sink.retainedHits().size(), numRetained);
    	    	assert_gt(sink.numHits(), lastHits);
    	    }
    	    assert_eq(0, sink.numProvisionalHits());
    	} else {
    		ebwtFw.search1MismatchOrBetter(s, params);
    	}
    	bool gotHits = sink.numHits() > lastHits;
	    
	    if(oneHit && gotHits) {
	    	assert_eq(0, sink.numProvisionalHits());
	    	uint32_t mElt = patid >> 3;
	    	if(mElt > length(doneMask)) {
	    		// Add 50% more elements, initialized to 0
	    		fill(doneMask, mElt + patid>>4, 0);
	    	}
			
			// Set a bit indicating this pattern is done and needn't be
			// considered by the 1-mismatch loop
	    	doneMask[mElt] |= (1 << (patid & 7));
	    	if(revcomp && params.fw()) {
	    		assert(patsrc.hasMorePatterns());
	    		assert(patsrc.nextIsReverseComplement());
	    		// Ignore this pattern (the reverse complement of
	    		// the one we just matched)
		    	const TStr& pat2 = patsrc.nextPattern();
		    	assert(!empty(pat2));
		    	patid++;
		    	// Set a bit indicating this pattern is done
		    	doneMask[patid >> 3] |= (1 << (patid & 7));
		    	if(qSameLen && length(pat2) != lastLen) {
		    		throw runtime_error("All reads must be the same length");
		    	}
		    	params.setFw(false);
		    	params.stats().incRead(s, pat2);
	    		assert(!patsrc.nextIsReverseComplement());
	    	} else if(revcomp) {
    	    	// The reverse-complement version hit, so retroactively
	    		// declare the forward version done
    	    	uint32_t mElt = (patid-1) >> 3;
    	    	if(mElt > length(doneMask)) {
    	    		// Add 50% more elements, initialized to 0
    	    		fill(doneMask, mElt + patid>>4, 0);
    	    	}
    	    	doneMask[mElt] |= (1 << ((patid-1) & 7));
	    	}
	    }
	    // Check all hits against a naive oracle
    	if(sanityCheck && !os.empty()) {
    	    vector<Hit>& hits = sink.retainedHits();
    	    // Accumulate hits found using a naive seed-and-extend into
    	    // sanityHits
    		findSanityHits(pat, spatid, sfw, os, sanityHits, true, false);
    		if(hits.size() > 0) {
    			// We hit, check that oracle also got our hits
        	    assert(!oneHit || hits.size() == 1);
    			if(oneHit && hits[0].mms > 0) {
					// If our oneHit hit is inexact, then there had
    				// better be no exact sanity hits
    				for(size_t i = 0; i < sanityHits.size(); i++) {
    					assert_gt(sanityHits[i].mms, 0);
    				}
    			}
    			reconcileHits(pat, spatid, sfw, os, hits, sanityHits, true, false);
    		} else {
    			// If we didn't hit, then oracle shouldn't have hit
        		assert_eq(0, sanityHits.size());
    		}
    		if(oneHit) {
    			// Ignore the rest of the oracle hits
    			sanityHits.clear(); 
    		} else {
    			// If in allHit mode, check that we covered *all* the
    			// hits produced by the oracle
    			checkSanityExhausted(pat, spatid, sfw, sanityHits, false);
    		}
    		assert_eq(0, sanityHits.size());
    	    // Check that orientation of hits squares with orientation
    	    // of the pattern searched
    	    for(size_t i = 0; i < hits.size(); i++) {
    	    	assert_eq(sfw, hits[i].fw);
    	    }
    	    sink.clearRetainedHits();
    	}
	    patid++;
    	lastHits = sink.numHits();
    }
	}
	// Release most of the memory associated with the forward Ebwt
    ebwtFw.evictFromMemory();
	{
		// Load the rest of (vast majority of) the backward Ebwt into
		// memory
		Timer _t(cout, "Time loading Backward Ebwt: ", timing);
		ebwtBw.loadIntoMemory();
	}
    patsrc.reset();          // reset pattern source to 1st pattern
    patsrc.setReverse(true); // reverse patterns
    params.setEbwtFw(false); // let search parameters reflect the reverse index
	// Sanity-check the restored version of the Ebwt
	if(sanityCheck && !os.empty()) {
		TStr rest; ebwtBw.restore(rest);
		uint32_t restOff = 0;
		for(size_t i = 0; i < os.size(); i++) {
			uint32_t olen = length(os[i]);
			for(size_t j = 0; j < olen; j++) {
				assert_eq(os[i][olen-j-1], rest[restOff]);
				restOff++;
			}
			uint32_t leftover = (restOff & ~ebwtBw.eh().chunkMask());
			uint32_t diff = ebwtBw.eh().chunkLen() - leftover;
			if(leftover != 0) restOff += diff;
			assert_eq(0, restOff & ~ebwtBw.eh().chunkMask());
		}
	}
	assert(patsrc.hasMorePatterns());
	assert(!patsrc.nextIsReverseComplement());
	patid = 0;       // start again from id 0
	lastHits = 0llu; // start again from 0 hits
	{
	Timer _t(cout, "Time for 1-mismatch backward search: ", timing);
    while(patsrc.hasMorePatterns() && patid < (uint32_t)qUpto) {
    	bool sfw = !revcomp || !patsrc.nextIsReverseComplement();
    	params.setFw(sfw);
    	uint32_t spatid = patid;
    	params.setPatId(spatid);
    	assert(!revcomp || (params.patId() & 1) == 0 || !params.fw());
    	assert(!revcomp || (params.patId() & 1) == 1 ||  params.fw());
    	TStr pat = patsrc.nextPattern();
    	assert(!empty(pat));
    	EbwtSearchState<TStr> s(ebwtBw, pat, params, seed);
    	params.stats().incRead(s, pat);
		// Skip if previous phase determined this read is "done"; this
		// should only happen in oneHit mode
    	if((doneMask[patid >> 3] & (1 << (patid & 7))) != 0) {
    		assert(oneHit);
    		patid++;
    		continue;
    	}
		patid++;
    	// Try to match with one mismatch while suppressing exact hits
    	ebwtBw.search1MismatchOrBetter(s, params, false /* suppress exact */);
    	sink.acceptProvisionalHits(); // automatically approve provisional hits
	    // If the forward direction matched with one mismatch, ignore
	    // the reverse complement
	    if(oneHit && revcomp && sink.numHits() > lastHits && params.fw()) {
    		assert(patsrc.nextIsReverseComplement());
    		assert(patsrc.hasMorePatterns());
    		// Ignore this pattern (the reverse complement of
    		// the one we just matched)
	    	const TStr& pat2 = patsrc.nextPattern();
	    	assert(!empty(pat2));
	    	patid++;
	    	params.setFw(false);
	    	params.stats().incRead(s, pat2);
    		assert(!patsrc.nextIsReverseComplement());
	    }
	    // Check that all hits are sane (NOT that all true hits were
	    // found - not yet, at least)
    	if(sanityCheck && !os.empty()) {
    	    vector<Hit>& hits = sink.retainedHits();
    	    // Accumulate hits found using a naive seed-and-extend into
    	    // sanityHits
    		findSanityHits(pat, spatid, sfw, os, sanityHits, false, true);
    		if(hits.size() > 0) {
    			// We hit, check that oracle also got our hits
    			reconcileHits(pat, spatid, sfw, os, hits, sanityHits, false, true);
    		} else {
    			// If we didn't hit, then oracle shouldn't have hit
        		assert_eq(0, sanityHits.size());
    		}
    		if(oneHit) {
    			// Ignore the rest of the oracle hits
    			sanityHits.clear(); 
    		} else {
    			// If in allHit mode, check that we covered *all* the
    			// hits produced by the oracle
    			checkSanityExhausted(pat, spatid, sfw, sanityHits, true);
    		}
    		assert_eq(0, sanityHits.size());
    	    // Check that orientation of hits squares with orientation
    	    // of the pattern searched
    	    for(size_t i = 0; i < hits.size(); i++) {
    	    	assert_eq(sfw, hits[i].fw);
    	    }
    	    sink.clearRetainedHits();
    	}
    	lastHits = sink.numHits();
    }
	}
}

template<typename TStr>
static void driver(const char * type,
                   const string& infile,
                   const string& query,
                   const vector<string>& queries,
                   const string& outfile)
{
	vector<TStr> ps;
	vector<TStr> os;
	// Read original string(s) from command-line if given (for sanity checking)
	if(sanityCheck && !origString.empty()) {
		if(origString.substr(origString.length()-4) == ".mfa" ||
		   origString.substr(origString.length()-3) == ".fa")
		{
			vector<string> origFiles;
			tokenize(origString, ",", origFiles);
			readSequenceFiles<TStr, Fasta>(origFiles, os);
		} else {
			readSequenceString(origString, os);
		}
	}
	// Seed random number generator
	srandom(seed);
	// Create a pattern source for the queries
	PatternSource<TStr> *patsrc = NULL;
	switch(format) {
		case FASTA:   patsrc = new FastaPatternSource<TStr> (queries, revcomp, false, patDumpfile, trim3, trim5); break;
		case FASTQ:   patsrc = new FastqPatternSource<TStr> (queries, revcomp, false, patDumpfile, trim3, trim5); break;
	    case BFQ:     patsrc = new BfqPatternSource<TStr>   (queries, revcomp, false, patDumpfile, trim3, trim5); break;
	    case SOLEXA:  patsrc = new SolexaPatternSource<TStr>(queries, revcomp, false, patDumpfile, trim3, trim5); break;
		case CMDLINE: patsrc = new VectorPatternSource<TStr>(queries, revcomp, false, patDumpfile, trim3, trim5); break;
		default: assert(false);
	}
	// Check that input is non-empty
	if(!patsrc->hasMorePatterns()) {
		cerr << "Error: Empty input!  Check that file format is correct." << endl;
		exit(1);
	}
	if(skipSearch) return;
	// Open hit output file
	ostream *fout;
	if(!outfile.empty()) {
		fout = new ofstream(outfile.c_str(), ios::binary);
	} else {
		fout = &cout;
	}
	// Initialize Ebwt object and read in header
    Ebwt<TStr> ebwt(infile, /* overriding: */ offRate, verbose, sanityCheck);
    assert_geq(ebwt.eh().offRate(), offRate);
    Ebwt<TStr>* ebwtBw = NULL;
    if(mismatches > 0) {
    	ebwtBw = new Ebwt<TStr>(infile + ".rev", /* overriding: */ offRate, verbose, sanityCheck);
    }
	if(sanityCheck && !os.empty()) {
		// Sanity check number of patterns and pattern lengths in Ebwt
		// against original strings
		assert_eq(os.size(), ebwt.nPat());
		for(size_t i = 0; i < os.size(); i++) {
			assert_eq(length(os[i]), ebwt.plen()[i]);
		}
	}
    // Load rest of (vast majority of) Ebwt into memory
	{
		Timer _t(cout, "Time loading Ebwt: ", timing);
	    ebwt.loadIntoMemory();
	}
	// Sanity-check the restored version of the Ebwt
	if(sanityCheck && !os.empty()) {
		TStr rest; ebwt.restore(rest);
		uint32_t restOff = 0;
		for(size_t i = 0; i < os.size(); i++) {
			for(size_t j = 0; j < length(os[i]); j++) {
				assert_eq(os[i][j], rest[restOff]);
				restOff++;
			}
			uint32_t leftover = (restOff & ~ebwt.eh().chunkMask());
			uint32_t diff = ebwt.eh().chunkLen() - leftover;
			if(leftover != 0) restOff += diff;
			assert_eq(0, restOff & ~ebwt.eh().chunkMask());
		}
	}
    // If sanity-check is enabled and an original text string
    // was specified, sanity-check the Ebwt by confirming that
    // its detransformation equals the original.
	if(sanityCheck && !os.empty()) {
		TStr rs; ebwt.restore(rs);
		TStr joinedo = Ebwt<TStr>::join(os, ebwt.eh().chunkRate(), seed, false);
		assert_eq(joinedo, rs);
	}
	{
		Timer _t(cout, "Time searching: ", timing);
		// Set up hit sink; if sanityCheck && !os.empty() is true,
		// then instruct the sink to "retain" hits in a vector in
		// memory so that we can easily sanity check them later on
		HitSink *sink;
		if(binOut)
			sink = new BufferedBinaryHitSink(
					*fout,
					revcomp,
					reportOpps,
					sanityCheck && !os.empty());
		else
			sink = new PrettyHitSink(
					*fout,
					revcomp,
					reportOpps,
					sanityCheck && !os.empty());
		EbwtSearchStats<TStr> stats;
		EbwtSearchParams<TStr> params(*sink,
		                              stats,
		                              (oneHit? MHP_PICK_1_RANDOM : MHP_CHASE_ALL),
		                              os,
		                              mismatches > 0);
		if(mismatches > 0) {
			// Search with mismatches
			if (kmer != -1 || allowed_diffs != -1)
				cerr << "1-mismatch ker extension not yet implemented, ignoring -k, -d" << endl;
				
			mismatchSearch(*patsrc, *sink, stats, params, ebwt, *ebwtBw, os);
			
		} else {
			if (kmer != -1)
			{
				vector<String<Dna, Packed<> > > ss;
				unpack(infile + ".3.ebwt", ss, NULL);
				// Search for hits on the 5' end, and then try to extend them
				// with a dynamic programming algorithm
				exactSearchWithExtension(ss, *patsrc, *sink, stats, params, ebwt, os);
			}
			else
			{
				// Search without mismatches
				exactSearch(*patsrc, *sink, stats, params, ebwt, os);
			}
		}
	    sink->finish(); // end the hits section of the hit file
	    if(printStats) {
		    // Write some high-level searching parameters and inputs
	    	// to the hit file
		    sink->out() << "Binary name: " << argv0 << endl;
		    sink->out() << "  Checksum: " << (uint64_t)(EBWT_SEARCH_HASH) << endl;
		    sink->out() << "Ebwt file base: " << infile << endl;
			sink->out() << "Sanity checking: " << (sanityCheck? "on":"off") << endl;
			sink->out() << "Verbose: " << (verbose? "on":"off") << endl;
		    sink->out() << "Queries: " << endl;
		    for(size_t i = 0; i < queries.size(); i++) {
		    	sink->out() << "  " << queries[i] << endl;
		    }
		    params.write(sink->out()); // write searching parameters
		    stats.write(sink->out());  // write searching statistics
		    _t.write(sink->out());     // write timing info
	    }
	    sink->flush();
		if(!outfile.empty()) {
			((ofstream*)fout)->close();
		}
		delete sink;
	}
}

/**
 * main function.  Parses command-line arguments.
 */
int main(int argc, char **argv) {
	string infile;  // read serialized Ebwt from this file
	string query;   // read query string(s) from this file
	vector<string> queries;
	string outfile; // write query results to this file
	parseOptions(argc, argv);
	argv0 = argv[0];
	Timer _t(cout, "Overall time: ", timing);

	// Get input filename
	if(optind >= argc) {
		cerr << "No input sequence, query, or output file specified!" << endl;
		printUsage(cerr);
		return 1;
	}
	infile = argv[optind++];
	
	// Get query filename
	if(optind >= argc) {
		cerr << "No query or output file specified!" << endl;
		printUsage(cerr);
		return 1;
	}
	query = argv[optind++];

	// Tokenize the list of query files
	tokenize(query, ",", queries);
	if(queries.size() < 1) {
		cerr << "Tokenized query file list was empty!" << endl;
		printUsage(cerr);
		return 1;
	}

	// Get output filename
	if(optind < argc) {
		outfile = argv[optind++];
	}
	if(outfile.empty() && binOut) {
		cerr << "When --binOut is specified, an output file must also be specified" << endl;
		printUsage(cerr);
		return 1;
	}

	// Optionally summarize
	if(verbose) {
		cout << "Input ebwt file: \"" << infile << "\"" << endl;
		cout << "Query inputs (DNA, " << file_format_names[format] << "):" << endl;
		for(size_t i = 0; i < queries.size(); i++) {
			cout << "  " << queries[i] << endl;
		}
		cout << "Output file: \"" << outfile << "\"" << endl;
		cout << "Local endianness: " << (currentlyBigEndian()? "big":"little") << endl;
		cout << "Sanity checking: " << (sanityCheck? "enabled":"disabled") << endl;
	#ifdef NDEBUG
		cout << "Assertions: disabled" << endl;
	#else
		cout << "Assertions: enabled" << endl;
	#endif
	}
	if(ipause) {
		cout << "Press key to continue..." << endl;
		getchar();
	}
	driver<String<Dna, Alloc<> > >("DNA", infile, query, queries, outfile);
    return 0;
}

template<typename TStr>
static void prioritySearch(PatternSource<TStr>& patsrc,
						   Ebwt<TStr>& ebwt,
						   vector<String<Dna, Packed<> > >* ss,
						   bool revcomp)

{
	uint32_t patid = 0;
	assert(patsrc.hasMorePatterns());
	
	list<SearchPolicy<TStr>*> search_heirarchy;
	
	
	// ExactSearchWithLowQualityThreePrime uses Landau Vishkin extension,
	// which assumes that a 5 prime hit has a difference on the 3 prime end.
	// Exact end to end matches will screw it up.  This is easy to change, but
	// I haven't done so yet for performance reasons.  For now, prioritySearch
	// screens out all exact matches first.
	
	search_heirarchy.push_back(new ExactSearch<TStr>());
	search_heirarchy.push_back(new ExactSearchWithLowQualityThreePrime<TStr>(*ss));
	
	HitSink hit_sink(cout, true);
	
	// Since I'm manually accumulating hits against both strands for a given
	// policy, I don't want the hit_sink to >> my patId for me.  I'll manage
	// the ids and corresponding hits myself.
	PrettyHitSink report_sink(cout, false, false);
	
	EbwtSearchStats<TStr> stats;
	EbwtSearchParams<TStr> params(hit_sink,
								  stats,
								  MHP_PICK_1_RANDOM,
								  false);
	
    while(patsrc.hasMorePatterns() && patid < (uint32_t)qUpto) 
	{
		
		// Grab a pattern...
		const TStr& pat = patsrc.nextPattern();
		assert(!empty(pat));
		
		TStr pat_rc;
		
		// ...and if we are doing RC, it's RC pattern from the pattern source.
		if (revcomp)
		{
			assert (patsrc.hasMorePatterns() && 
					patsrc.nextIsReverseComplement());
			pat_rc = patsrc.nextPattern();
			assert(!empty(pat_rc));
			//cerr << "\tfwd: " << pat << endl;
			//cerr << "\trev: " << pat_rc << endl;
		}
		
		params.setPatId(patid++);
		
		// Search for hits, stopping at the first (i.e. highest priority) 
		// matching policy for which there is at least one hit.
		for (typename list<SearchPolicy<TStr>*>::iterator pol_itr = search_heirarchy.begin();
			 pol_itr != search_heirarchy.end(); 
			 ++pol_itr)
		{
			SearchPolicy<TStr>* policy = *pol_itr;
			params.setFw(true);
			policy->search(ebwt, stats, params, pat, hit_sink);
			if (revcomp)
			{
				params.setFw(false);
				policy->search(ebwt, stats, params, pat_rc, hit_sink);
			}
			
			vector<Hit>& hits = hit_sink.retainedHits();
			if (hits.size())
			{
				// FIXME: this is a bullshit reporting policy.  We need a real 
				// one.
				Hit& hit = hits[0];
				report_sink.reportHit(hit.h, hit.pat, hit.fw);
				break;
			}
		}
		
		hit_sink.clearRetainedHits();
	}
	
	report_sink.finish();
	
	for (typename list<SearchPolicy<TStr>*>::iterator pol_itr = search_heirarchy.begin();
		 pol_itr != search_heirarchy.end(); 
		 ++pol_itr)
	{
		delete *pol_itr;
	}
}


#endif
