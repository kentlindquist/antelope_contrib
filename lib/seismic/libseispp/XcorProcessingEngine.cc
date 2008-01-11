//  Peng Wang, HPC/RAC/UITS
//  Indiana University
//
//  Copyright 2005, The Trustees of Indiana University.
//  Last Modified: 12/1/2005

#include <algorithm>
#include "SeisppKeywords.h"
#include "XcorProcessingEngine.h"


const string gain_keyword("gain");
const string dbarrival_lag_keyword("dbarrival_lag");
/* This is a helper function intended to be used only with file scope.
It is used to implement trace shifting with a set of existing arrivals
loaded from the database.  The algorithm is complicated by the fact with 
need scan them all once, compute and subtract a mean, and then set a
lag.  This is necessary to prevent a large skew between arrivals previously
set in the db and those missing.  Reason is that origin errors can produce
large average shifts in arrival time.  Result is set in the trace headers
keyed by the keyword set in dbarrival_lag_keyword immediately above.  
LagShift is then called to shift all data by the computed values.

Note this routine intentionally tests no Metadata attributes or existence
because it assumes they are already set.  This is done for efficiency.
Do not transport this procedure to another program without changes or
attention to this detail
*/
void dbarrival_shift(TimeSeriesEnsemble& d)
{
	vector<TimeSeries>::iterator dptr;
	vector<double> atimes;
	double at,atmean,pt;
	int count;
	for(dptr=d.member.begin(),atmean=0.0,count=0;
			dptr!=d.member.end();++dptr)
	{
	    if(dptr->live)
	    {
		at=dptr->get_double(dbarrival_time_key);
		pt=dptr->get_double(predicted_time_key);
		if(at>0)
		{
			++count;
			atmean += at-pt;
			atimes.push_back(at);
		}
	    }
	}
	atmean/=static_cast<double>(count);
	/* Now compute the shifts and post them keyed by dbarrival_lag_keyword*/
	for(dptr=d.member.begin();dptr!=d.member.end();++dptr)
	{
		at=dptr->get_double(dbarrival_time_key);
		if(at>0 && dptr->live)
		{
			pt=dptr->get_double(predicted_time_key);
			at=at-pt-atmean;
			dptr->put(dbarrival_lag_keyword,at);
		}
		else
			dptr->put(dbarrival_lag_keyword,MoveoutBad);
	}
   	LagShift(d,dbarrival_lag_keyword,arrival_time_key);
}
XcorProcessingEngine::XcorProcessingEngine(Pf * global_pf, 
	XcorAnalysisSetting asinitial,
		string waveform_db_name,
			string result_db_name) 
			  : rdef(global_pf),am("css3.0"),analysis_setting(asinitial)
{
	int i, j;
	try {

		global_md=Metadata(global_pf);
		pf_used_by_engine = pfdup(global_pf);
		string schema=global_md.get_string("schema");
		if(schema!="css3.0")
	 		am=AttributeMap(schema);
	
		trace_mdl=pfget_mdlist(global_pf,"trace_mdl");
		ensemble_mdl=pfget_mdlist(global_pf,"trace_mdl");
	
		waveform_db_handle=DatascopeHandle(waveform_db_name,true);
		// deal with possibility that output db is the same
		// as the input db.  This logic depends upon the 
		// current API for Datascope in which the handle is 
		// Datascope like being freely copyable.  
		if (waveform_db_name==result_db_name) 
			result_db_handle=DatascopeHandle(waveform_db_handle);
		else
			result_db_handle=DatascopeHandle(result_db_name,false);
		dbassoc=dblookup(result_db_handle.db,0,(char *) "assoc",0,0);
		dbarrival=dblookup(result_db_handle.db,0,(char *) "arrival",0,0);
		dbxcorarrival=dblookup(result_db_handle.db,0,(char *) "xcorarrival",0,0);
		dbxcorbeam=dblookup(result_db_handle.db,0,(char *) "xcorbeam",0,0);
		dbwfprocess=dblookup(result_db_handle.db,0,(char *) "wfprocess",0,0);
		dbevlink=dblookup(result_db_handle.db,0,(char *) "evlink",0,0);
		if( (dbassoc.table==dbINVALID) 
			|| (dbarrival.table==dbINVALID))
		{
			result_db_handle.close();
			waveform_db_handle.close();
			throw SeisppError(string("XcorProcessingEngine:")
				+string("  constructor had problems opening one or more database tables"));
		}
		if( (dbxcorarrival.table==dbINVALID)
			|| (dbxcorbeam.table==dbINVALID)
			|| (dbevlink.table==dbINVALID)
			|| (dbwfprocess.table==dbINVALID) )
		{
			save_extensions=false;
			cerr << "XcorProcessingEngine (Warning):  Extension tables not defined"
				<< " only arrival and assoc will be saved in database"<<endl;
		}
		else
			save_extensions=true;
		// Verify site and sitechan are defined and abort if
		// they are empty
		int ntest;
		Dbptr dbtmp=dblookup(waveform_db_handle.db,0,(char *) "site",0,0);
		dbquery(dbtmp,dbRECORD_COUNT,&ntest);
		if(ntest<=0) 
			throw SeisppError(string("XcorProcessingEngine:")
				+string(" required site table is empty"));
		dbtmp=dblookup(waveform_db_handle.db,0,(char *) "sitechan",0,0);
		dbquery(dbtmp,dbRECORD_COUNT,&ntest);
		if(ntest<=0) 
			throw SeisppError(string("XcorProcessingEngine:")
				+string(" required sitechan table is empty"));
		// These two lists are largely fixed, but an example of the
		// use of pf to increase flexibility in future reuse.
		MetadataList mdlassoc=pfget_mdlist(global_pf,"save_list_assoc");
		MetadataList mdlarrival=pfget_mdlist(global_pf,"save_list_arrival");
		arru=ArrivalUpdater(result_db_handle,mdlassoc,mdlarrival,schema);
		// This is needed for wfprocess table saving of array beam
		beam_mdl = pfget_mdlist(global_pf,"BeamMetadataList");
		beam_directory=global_md.get_string("beam_directory");
		beam_dfile=global_md.get_string("beam_dfile");
	
	        string time_stamp=global_md.get_string("initial_time_stamp");
	        double treference=str2epoch(const_cast<char *>
	                        (time_stamp.c_str()));
		netname=global_md.get_string("network_name");
		stations= SeismicArray(dynamic_cast<DatabaseHandle&>(waveform_db_handle),
                                        treference,netname);
		if(stations.array.size()<=0)
			throw SeisppError(string("XcorProcessingEngine:  ")
			  + string("Found no stations marked on at event time"));

		use_subarrays=global_md.get_bool("use_subarrays");
		/* We always load subarray definitions even if they are turned 
		off initially.  This allows toggling between full and subarray
		processing in interactive mode. */
		load_subarrays_from_pf(stations,global_pf);
		current_subarray=0;
		if(stations.number_subarrays()<=0)
			throw SeisppError(string("XcorProcessingEngine: ")
			 +string("pf error.  subarray definitions are required even if subarrays are initially off\n")
			+string("Add entries for virtual_arrays &Arr{} and try again"));
		// A bit inefficient, but useful to test up front to be sure this works for
		// small cost in execution time
		SeismicArray test=stations.subset(0);
		current_subarray_name=test.name;
	
		raw_data_twin.start=global_md.get_double("data_window_start");
		raw_data_twin.end=global_md.get_double("data_window_end");
		current_data_window.start=treference+raw_data_twin.start;
		current_data_window.end=treference+raw_data_twin.end;
	        target_dt=global_md.get_double("target_sample_interval");
		// These set up separate display parameters for each 
		// display window (data, beam, xcor result);
		data_display_md=Metadata(global_pf,"data_window_parameters");
		xcor_display_md=Metadata(global_pf,"correlation_window_parameters");
		beam_display_md=Metadata(global_pf,"beam_window_parameters");
		// Some useful cutoff parameters
		xcorpeak_cutoff=global_md.get_double("correlation_peak_cutoff");
		coherence_cutoff=global_md.get_double("coherence_cutoff");
		stack_weight_cutoff=global_md.get_double("stack_weight_cutoff");
		xcorpeak_cutoff_default=xcorpeak_cutoff;
		coherence_cutoff_default=coherence_cutoff;
		stack_weight_cutoff_default=stack_weight_cutoff;
		time_lag_cutoff=global_md.get_double("time_lag_cutoff");
		// For three component data we need to create this 
		// StationChannelMap object that tells us how to map channel
		// codes into components
		RequireThreeComponents=global_md.get_bool("RequireThreeComponents");
		if(RequireThreeComponents)
			stachanmap=StationChannelMap(global_pf);
		else
			stachanmap=StationChannelMap();
		mcc = NULL;   // Need this unless we can convert to a shared_ptr;
		autoscale_initial=global_md.get_bool("AutoscaleInitialPlot");
		load_arrivals=global_md.get_bool("LoadArrivals");

	} catch (MetadataGetError mderr)
	{
		mderr.log_error();
		throw SeisppError("XcorProcessingEngine construction failed");
	}
	catch (SeisppError serr)
	{
		throw serr;
	}
	catch (...)
	{
		throw SeisppError("XcorProcessingEngine constructor:  Unknown error was thrown");
	}
}
//
// It would be nice to get rid of mcc as a raw pointer here.  We should
// either invoke a copy overhead or use a resource managing pointer shared_ptr.
//  This is a typically dangerous use of wild pointers.
//
XcorProcessingEngine::~XcorProcessingEngine()
{
	if(mcc!=NULL) delete mcc;
	// There is a serious problem here with destruction related to
	// a design flaw in DatascopeHandle.  Because we need to keep
	// several tables open in potentially different databases there
	// is no clear way to handle the destruction.  The important thing
	// this means is an XcorProcessingEngine should be created only
	// once and never destroyed until the program is ready to exit.
	// Multiple create destroys are guaranteed problems.  Should
	// be fixed eventually, but the correct solution is in the 
	// DatascopeHandle code itself, not here.,
}
/* Small helper needed below.  Looks for bad moveout flag
 and resets moveout to 0.0.  Needed to keep cross correlation
 plot from crashing.  Kind of a hack solution.  Need a cleaner
 way to handle this perhaps.  */

void ResetMoveout(TimeSeriesEnsemble& d)
{
	vector<TimeSeries>::iterator dptr;

	for(dptr=d.member.begin();dptr!=d.member.end();++dptr)
	{
		double moveout;
		if(dptr->live)
		{
			moveout=dptr->get_double(moveout_keyword);
			if(fabs(moveout)>MoveoutBadTest)
			{
				moveout=0.0;
				dptr->put(moveout_keyword,moveout);
			}
		}
	}
}
template <class T, SortOrder SO> struct less_metadata_double
                : public binary_function<T,T,bool> {
        bool operator()(T x, T y)
        {
		string keyword;
		switch (SO)
		{
		case COHERENCE:
			keyword=SEISPP::coherence_keyword;
			break;
		case CORRELATION_PEAK:
			keyword=SEISPP::peakxcor_keyword;
			break;
		case AMPLITUDE:
			keyword=SEISPP::amplitude_static_keyword;
			break;
		case LAG:
			keyword=SEISPP::moveout_keyword;
			break;
  		case SITE_LAT:
			keyword=string("site.lat");
			break;
		case SITE_LON:
			keyword=string("site.lon");
			break;
		case PREDARR_TIME:
			keyword=predicted_time_key;
			break;
		case ESAZ:
			keyword=string("esaz");;
			break;
		case DISTANCE:
			keyword=string("distance");;
			break;
		case DBARRIVAL_TIME:
			keyword=dbarrival_time_key;
			break;
		case ARRIVAL_TIME:
			keyword=arrival_time_key;
			break;
		case SNR:
			keyword=snr_keyword;
			break;
		case WEIGHT:
		default:
			keyword=SEISPP::stack_weight_keyword;
			break;
		}
		/* the try-catch logic below assure that data that
		return errors on a get act as if they are the smallest possible value */
		double valx,valy;
		try { 
			valx=x.get_double(keyword);
		}catch(...) {return true;};
		try { 
			valy=y.get_double(keyword);
		}catch(...) {return false;};
	
	        if(valx<valy)
	                return true;
	        else
	                return false;
        }
};
template <class T, SortOrder SO> struct greater_metadata_double
                : public binary_function<T,T,bool> {
        bool operator()(T x, T y)
        {
		string keyword;
		switch (SO)
		{
		case COHERENCE:
			keyword=SEISPP::coherence_keyword;
			break;
		case CORRELATION_PEAK:
			keyword=SEISPP::peakxcor_keyword;
			break;
		case AMPLITUDE:
			keyword=SEISPP::amplitude_static_keyword;
			break;
		case LAG:
			keyword=SEISPP::moveout_keyword;
			break;
  		case SITE_LAT:
			keyword=string("site.lat");
			break;
		case SITE_LON:
			keyword=string("site.lon");
			break;
		case PREDARR_TIME:
			keyword=predicted_time_key;
			break;
		case ESAZ:
			keyword=string("esaz");;
			break;
		case DISTANCE:
			keyword=string("distance");;
			break;
		case DBARRIVAL_TIME:
			keyword=dbarrival_time_key;
			break;
		case ARRIVAL_TIME:
			keyword=arrival_time_key;
			break;
		case SNR:
			keyword=snr_keyword;
			break;
		case WEIGHT:
		default:
			keyword=SEISPP::stack_weight_keyword;
			break;
		}
		/* the try-catch logic below assure that data that
		return errors on a get act as if they are the largest possible value */
		double valx,valy;
		try { 
			valx=x.get_double(keyword);
		}catch(...) {return true;};
		try { 
			valy=y.get_double(keyword);
		}catch(...) {return false;};
	
	        if(valx>valy)
	                return true;
	        else
	                return false;
        }
};

/* Small helper for analyze() method below.  snr cannot be computed
in this algorithm until after the MultichannelCorrelator object is
created.  We want the snr attributes stored with the xcor functions
to allow sorting the ensemble by snr externally.  This makes sure
this is done for all traces.  dead traces are posted too, but a 
they are maked with snr of -1.0.  A magic number, but should be
ok in this context. */
void copy_SNR_to_xcor(TimeSeriesEnsemble& d, TimeSeriesEnsemble& xcor,
	const string keyword)
{
	vector<TimeSeries>::iterator id,ix;
	double snr;
	double snrbad(-1.0);
	for(id=d.member.begin(),ix=xcor.member.begin();
		id!=d.member.end() && ix!=xcor.member.end();
			++id,++ix)
	{
		if(id->live)
		{
			snr=id->get_double(keyword);
			ix->put(keyword,snr);
		}
		else
			ix->put(keyword,snrbad);
	}
}

MultichannelCorrelator *XcorProcessingEngine::XcorProcessingEngine :: analyze()
{
   if(mcc!=NULL) delete mcc;
   try {
     UpdateGeometry(current_data_window);
     mcc=	new MultichannelCorrelator(waveform_ensemble,RobustStack,analysis_setting.beam_tw,
   	analysis_setting.robust_tw,time_lag_cutoff,RobustSNR,NULL,
   	analysis_setting.reference_trace); 
   } catch (...) {throw;};
   //
   // We need to reset bad moveout in xcor so it can be plotted properly.
   // 
   ResetMoveout(mcc->xcor);
   // This permanently alters the time base for the xcor result.
   // All users of this must be aware time base for xcor is not longer
   // related to a fixed time base.  Aim is to produce a gather that
   // is aligned to zero lag that can inspected graphically.
   mcc->xcor=MoveoutTimeShift(mcc->xcor);
   // This aligns the ensemble by the time shifts computed by mcc operator.
   // WARNING:  the frozen arrival.time key used here is a long-term
   // maintenance issue and limits future reuse.
   // ALSO very important to realize arrival.time MUST HAVE BEEN INITIALIZED
   // original to a predicted arrival time when running from raw data
   // (the current situation for teleseismic data).
   LagShift(waveform_ensemble,moveout_keyword,arrival_time_key);
   // compute signal to noise ratio for each trace and post to metadata 
   ensemble_SNR_rms<TimeSeriesEnsemble,TimeSeries>(waveform_ensemble,analysis_setting.beam_tw,snr_keyword);
   if(waveform_ensemble.member.size()==mcc->xcor.member.size())
	copy_SNR_to_xcor(waveform_ensemble,mcc->xcor,snr_keyword);
   else
	throw SeisppError(string("XcorProcessingEngine::analyze():  ")
		+ string("size mismatch of waveform and xcor ensembles\n")
		+ string("This is a coding error.  Report to pavlis@indiana.edu"));
   // Auto scale data using computed amplitude set by MultichannelCorrelator
   ScaleEnsemble<TimeSeriesEnsemble,TimeSeries>(waveform_ensemble,
	amplitude_static_keyword,true);
   // reset gain so amplitude statics can be properly accumulated
   // if data are reprocessed
   ScaleCalib <TimeSeriesEnsemble>(waveform_ensemble,
		gain_keyword,amplitude_static_keyword);
   return(mcc);
}

void XcorProcessingEngine::sort_ensemble()
{
   switch(analysis_setting.result_sort_order)
   {
   case COHERENCE:
	if(analysis_setting.    sort_reverse)
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		greater_metadata_double<TimeSeries,COHERENCE>());
	else
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		less_metadata_double<TimeSeries,COHERENCE>());
	break;
   case CORRELATION_PEAK:
	if(analysis_setting.    sort_reverse)
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		greater_metadata_double<TimeSeries,CORRELATION_PEAK>());
	else
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		less_metadata_double<TimeSeries,CORRELATION_PEAK>());
	break;
   case AMPLITUDE:
	if(analysis_setting.    sort_reverse)
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		greater_metadata_double<TimeSeries,AMPLITUDE>());
	else
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		less_metadata_double<TimeSeries,AMPLITUDE>());
	break;
   case LAG:
	if(analysis_setting.    sort_reverse)
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		greater_metadata_double<TimeSeries,LAG>());
	else
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		less_metadata_double<TimeSeries,LAG>());
	break;
   case WEIGHT:
	if(analysis_setting.    sort_reverse)
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		greater_metadata_double<TimeSeries,WEIGHT>());
	else
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
		less_metadata_double<TimeSeries,WEIGHT>());
	break;
   case SITE_LAT:
	if(analysis_setting.    sort_reverse)
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,SITE_LAT>());
	else
	    sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,SITE_LAT>());
        break;
   case SITE_LON:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,SITE_LON>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,SITE_LON>());
        break;
   case PREDARR_TIME:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,PREDARR_TIME>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,PREDARR_TIME>());
        break;
   case ESAZ:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,ESAZ>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,ESAZ>());
        break;
   case ARRIVAL_TIME:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,ARRIVAL_TIME>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,ARRIVAL_TIME>());
	break;
   case DBARRIVAL_TIME:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,DBARRIVAL_TIME>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,DBARRIVAL_TIME>());
	break;
   case SNR:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,SNR>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,SNR>());
	break;
   case DISTANCE:
	if(analysis_setting.    sort_reverse)
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                greater_metadata_double<TimeSeries,DISTANCE>());
	else
            sort(waveform_ensemble.member.begin(),waveform_ensemble.member.end(),
                less_metadata_double<TimeSeries,DISTANCE>());
	break;
   default:
	cerr << "Illegal     sort order.  Original order preserved."<<endl;
   }
   if(mcc!=NULL)
   {
      switch(analysis_setting.result_sort_order)
      {
      case COHERENCE:
	if(analysis_setting.    sort_reverse)
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		greater_metadata_double<TimeSeries,COHERENCE>());
	else
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		less_metadata_double<TimeSeries,COHERENCE>());
   	break;
      case CORRELATION_PEAK:
	if(analysis_setting.    sort_reverse)
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		greater_metadata_double<TimeSeries,CORRELATION_PEAK>());
	else
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		less_metadata_double<TimeSeries,CORRELATION_PEAK>());
   	break;
      case AMPLITUDE:
	if(analysis_setting.    sort_reverse)
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		greater_metadata_double<TimeSeries,AMPLITUDE>());
	else
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		less_metadata_double<TimeSeries,AMPLITUDE>());
   	break;
      case LAG:
	if(analysis_setting.    sort_reverse)
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		greater_metadata_double<TimeSeries,LAG>());
	else
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		less_metadata_double<TimeSeries,LAG>());
   	break;
      case WEIGHT:
	if(analysis_setting.    sort_reverse)
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		greater_metadata_double<TimeSeries,WEIGHT>());
	else
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
   		less_metadata_double<TimeSeries,WEIGHT>());
   	break;
      case SITE_LAT:
	if(analysis_setting.    sort_reverse)
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   greater_metadata_double<TimeSeries,SITE_LAT>());
	else
   	    sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   less_metadata_double<TimeSeries,SITE_LAT>());
           break;
      case SITE_LON:
	if(analysis_setting.    sort_reverse)
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   greater_metadata_double<TimeSeries,SITE_LON>());
	else
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   less_metadata_double<TimeSeries,SITE_LON>());
           break;
      case PREDARR_TIME:
	if(analysis_setting.    sort_reverse)
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   greater_metadata_double<TimeSeries,PREDARR_TIME>());
	else
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   less_metadata_double<TimeSeries,PREDARR_TIME>());
           break;
      case ESAZ:
	if(analysis_setting.    sort_reverse)
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   greater_metadata_double<TimeSeries,ESAZ>());
	else
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   less_metadata_double<TimeSeries,ESAZ>());
           break;
      case SNR:
	if(analysis_setting.    sort_reverse)
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   greater_metadata_double<TimeSeries,SNR>());
	else
               sort(mcc->xcor.member.begin(),mcc->xcor.member.end(),
                   less_metadata_double<TimeSeries,SNR>());
           break;
      default:
   	cerr << "Illegal sort order.  Original order preserved."<<endl;
      }
   }
}
// The next three functions are somewhat unnecessary with
// this implementation, but are useful as interface routines
// nonetheless.
TimeSeries XcorProcessingEngine::get_beam()
{
    return(mcc->ArrayBeam());
}


MultichannelCorrelator *XcorProcessingEngine::get_correlation_results()
{
    return (mcc);
}
TimeSeriesEnsemble XcorProcessingEngine::get_waveforms()
{
	return(waveform_ensemble);
}
TimeSeriesEnsemble XcorProcessingEngine::get_raw_data()
{
	return(*regular_gather);
}
// Local function to extract a particular component from a 3c ensemble
// returning a scalar time series ensemble that can be passed downstream
// for processing by this program.  
auto_ptr<TimeSeriesEnsemble> Convert3CEnsemble(ThreeComponentEnsemble *tcse,
			string compname,Hypocenter hypo,string phase)
{
	int i;
	TimeSeries *x;
	// Hard wired for now.  May be a parameter eventually
	const double vp0def(6.0),vs0def(3.5);
	auto_ptr<TimeSeriesEnsemble> result(new
		TimeSeriesEnsemble(dynamic_cast<Metadata&> (*tcse),
				tcse->member.size()));
	if(compname.find_first_of("ZNE")!=string::npos)
	{
	    for(i=0;i<tcse->member.size();++i)
	    {
		if(!tcse->member[i].live) continue;
		// First make certain we are in cardinal coordinates
		tcse->member[i].rotate_to_standard();
		if(compname=="E")
			x=ExtractComponent(tcse->member[i],0);
		else if(compname=="N")
			x=ExtractComponent(tcse->member[i],1);
		else
			x=ExtractComponent(tcse->member[i],2);
		result->member.push_back(*x);
		delete x;
	    }
	}
	else if(compname.find_first_of("TRL")!=string::npos)
	{
	    for(i=0;i<tcse->member.size();++i)
            {
		if(!tcse->member[i].live) continue;
		double vp0,vs0;
		double stalat,stalon,staelev;
		try {
			tcse->member[i].rotate_to_standard();
		} catch (SeisppError serr)
		{
			serr.log_error();
			string staname=tcse->member[i].get_string("sta");
			cerr << "Station = "<< staname
				<<" data deleted from ensemble"<<endl;
			continue;
		}
		try {
			vp0=tcse->member[i].get_double("vp0");
			vs0=tcse->member[i].get_double("vs0");
		} catch (MetadataGetError mde) {
			vp0=vp0def;
			vs0=vs0def;
		}
		try {
			stalat=tcse->member[i].get_double("lat");
			stalon=tcse->member[i].get_double("lon");
			staelev=tcse->member[i].get_double("elev");
			// We store latitude and longitude in attributes
			// in degrees, but we need to convert them to
			// radians for internal use
			stalat=rad(stalat);
			stalon=rad(stalon);
		}
		catch (MetadataGetError mde)
		{
			mde.log_error();
			throw SeisppError(string("XcorProcessingEngine:")
				+string("  Failure in three component processing"));
			
		}
		SlownessVector u=hypo.phaseslow(stalat,stalon,staelev,phase);
		try {
			tcse->member[i].free_surface_transformation(u,vp0,vs0);
		} catch (SeisppError serr)
		{
			serr.log_error();
			cerr << "Using simple ray coordinates for station "
				<< tcse->member[i].get_string("sta")
				<<endl;
			SphericalCoordinate sc=PMHalfspaceModel(vp0,vs0,
				u.ux,u.uy);
			tcse->member[i].rotate(sc);
		}
		if(compname=="T")
			x=ExtractComponent(tcse->member[i],0);
		else if(compname=="R")
			x=ExtractComponent(tcse->member[i],1);
		else
			x=ExtractComponent(tcse->member[i],2);
		result->member.push_back(*x);
		delete x;
	    }
	}
	else
		throw SeisppError(string("XcorProcessingEngine:")
			+ string("Cannot handle component name=")
			+ compname);
	return result;
}
void XcorProcessingEngine::load_data(Hypocenter & h)
{
    try {
	// It is necessary to clear the contents of mcc in
	// some situations.  In particular, in the gui dbxcor
	// we desire sorting the data after it is read.  The
	// sort algorithm in XcorProcessingEngine aims to sort
	// xcor traces in the same order as data.  We need to clear
	// it here to allow it to bypass this until mcc is created.
	if(mcc!=NULL)
	{
		delete mcc;
		mcc=NULL;
	}
	current_data_window=TimeWindow(h.time+raw_data_twin.start,
		h.time+raw_data_twin.end);
	UpdateGeometry(current_data_window);
	// Read raw data.  Using an auto_ptr as good practice
	// 3c mode can work for either cardinal directions or
	// applying transformations.  In either case we use
	// the temporary auto_ptr to hold the data read in
	auto_ptr<TimeSeriesEnsemble> tse;
	if(RequireThreeComponents)
	{
		string chan_allowed("ZNELRT");
		if(analysis_setting.component_name
			.find_first_of(chan_allowed,0)<0)
		{
			throw SeisppError(
				string("XcorProcessingEngine::load_data:")
				+ string(" Illegal channel code specified.")
				+ string(" Must be Z,N,E,L,R, or T") );
		}
		ThreeComponentEnsemble *tcse;
		tcse = array_get_data(
  			   stations,
                           h,
			   analysis_setting.phase_for_analysis,
                           raw_data_twin,
                           analysis_setting.tpad,
                           dynamic_cast<DatabaseHandle&>(waveform_db_handle),
			   stachanmap,
                           ensemble_mdl,
                           trace_mdl,
                           am);
		// This specialized function could be generalized, but
		// done in one pass here for efficiency.  Local function
		// to this file found above
		tse=auto_ptr<TimeSeriesEnsemble>(Convert3CEnsemble(tcse,
			analysis_setting.component_name,h,
			analysis_setting.phase_for_analysis));	
		delete tcse;
	}
	else
	{
		tse=auto_ptr<TimeSeriesEnsemble>(array_get_data(
  			   stations,
                           h,
			   analysis_setting.phase_for_analysis,
			   analysis_setting.component_name,
                           raw_data_twin,
                           analysis_setting.tpad,
                           dynamic_cast<DatabaseHandle&>(waveform_db_handle),
                           ensemble_mdl,
                           trace_mdl,
                           am));
	}
	StationTime predarr=ArrayPredictedArrivals(stations,h,
		analysis_setting.phase_for_analysis); 

	// Following not needed because regular_gather is now
	// stored as an auto_ptr.
	//if (regular_gather != NULL) { delete regular_gather; regular_gather=NULL;}
	regular_gather=auto_ptr<TimeSeriesEnsemble>
			(AssembleRegularGather(*tse,predarr,
			analysis_setting.phase_for_analysis,
            		analysis_setting.gather_twin,target_dt,rdef,true));
	/* Load arrival times if requested */
	if(load_arrivals)
	{
		/* This is a bit of a misuse of this constructor, but it will
		work in this context in the current implemenation.  Beware a
		possible maintenance issue */
		DatascopeHandle dbh(dbarrival,dbarrival);
		/*We are using two frozen constants here that are less than
		idea.  20.0 is used as the time pad for grabbing relevant
		arrivals.  -1.0 is used to set a null value for data with
		no arrivals.  Works here as below we can then test for a
		negative value to indicate a null. */
		LoadEventArrivals<TimeSeriesEnsemble>(*regular_gather,
			dynamic_cast<DatabaseHandle&>(dbh),
			 analysis_setting.phase_for_analysis,
			  predicted_time_key,
			   dbarrival_time_key,
			      20.0,
				-1.0);
	}
	// This should probably be in the libseispp library, but
	// we'll hard code it here for now.  Need to set defaults
	// on all the computed metadata for all members of the
	// ensemble.  Without this there can be state issues about
	// whether these are loaded that cause metadata related
	// exceptions to be thrown.
	for(int i=0;i<regular_gather->member.size();++i)
	{
		double atime;
		double lat,lon;
		regular_gather->member[i].put(coherence_keyword,0.0);
		regular_gather->member[i].put(stack_weight_keyword,0.0);
		regular_gather->member[i].put(peakxcor_keyword,0.0);
		regular_gather->member[i].put(amplitude_static_keyword,0.0);
		regular_gather->member[i].put(moveout_keyword,MoveoutBad);
		/* Initialize arrival_time_key to predicted arrival time which
		is the time 0 mark for the data at this stage.  Note this
		time is dithered throughout this processing, but we maintain
		a time standard by simultaneously dithering this attribute.*/
		atime=regular_gather->member[i].get_double(predicted_time_key);
		regular_gather->member[i].put(arrival_time_key,atime);
	}
	/* Shift traces by measured arrival times if requested.  */
	if(load_arrivals) dbarrival_shift(*regular_gather);
	for(int i=0;i<regular_gather->member.size();++i)
	{
		double lat,lon;
		double seaz,esaz,distance;
		try {
			lat=regular_gather->member[i].get_double("lat");
			lon=regular_gather->member[i].get_double("lon");
			/* these are loaded in degrees.  We must convert
			them to radians */
			lat=rad(lat);
			lon=rad(lon);
		}
		catch(MetadataGetError mde)
		{
			mde.log_error();
			cerr << "In XcorProcessEngine::load_data:  continuing with lat,lon=0"<<endl;
			lat=0.0;   lon=0.0;
		}
		seaz=h.seaz(lat,lon);
		esaz=h.esaz(lat,lon);
		distance=h.distance(lat,lon);
		regular_gather->member[i].put("seaz",deg(seaz));
		regular_gather->member[i].put("esaz",deg(esaz));
		regular_gather->member[i].put("distance",distance);
		regular_gather->member[i].put("distance_deg",deg(distance));
	}
	// post netname here.  Overridden below for subarrays 
	// but this sets it for full array mode
	regular_gather->put("netname",netname);
	
	
	// Load filtered data into waveform_ensemble copy
	// of this data.
	if(use_subarrays)
	{
		int nsubs=stations.number_subarrays();
		auto_ptr<TimeSeriesEnsemble> csub;
		for(current_subarray=0;current_subarray<nsubs;
				++current_subarray)
		{
			SeismicArray subnet=stations.subset(current_subarray);
			csub=ArraySubset(*regular_gather,subnet);
			// post subarray name as netname in ensemble
			// Convenient if mysterious way to get this 
			// to save procedure
			csub->put("netname",subnet.name);
			// Need to set the name too
			current_subarray_name=subnet.name;
			if((*csub).member.size()>0) break;
		} 
		if(current_subarray>=nsubs) 
			throw SeisppError(
				string("XcorProcessingEngine::load_data: ")
				+ string("  error in subarray definitions.  ")
				+ string("All subarrays have no data for this event"));
		waveform_ensemble=*csub;
	}
	else
	{
		waveform_ensemble=*regular_gather;
	}
	FilterEnsemble(waveform_ensemble,analysis_setting.filter_param);
	if(autoscale_initial)
	{
		MeasureEnsemblePeakAmplitudes<TimeSeriesEnsemble,TimeSeries>
			(waveform_ensemble,gain_keyword);
		ScaleEnsemble<TimeSeriesEnsemble,TimeSeries>
			(waveform_ensemble,gain_keyword,true);
		ScaleCalib<TimeSeriesEnsemble>
			(waveform_ensemble,gain_keyword,amplitude_static_keyword); 
	}
	else
	{
		double initial_gain=1.0;
		InitializeEnsembleAttribute<TimeSeriesEnsemble,double>
			(waveform_ensemble,gain_keyword,initial_gain);
	}
	
	// We need to always reset these
	xcorpeak_cutoff=xcorpeak_cutoff_default;
	coherence_cutoff=coherence_cutoff_default;
	stack_weight_cutoff=stack_weight_cutoff_default;
    }
    catch (...) {throw;}
}
// This applies a second filter to the data beyond the base filter.  
// This is often necessary. e.g. demean followed by integration.
//
void XcorProcessingEngine::filter_data(TimeInvariantFilter f)
{
	FilterEnsemble(waveform_ensemble,f);
}
/* Small companion to save_results to edit an input channelo code to 
 produce a channel code that will normaly resolve to valid sitchan entries.
 Algorithm used will always replace character 3 in the input chan unless
 the the original chan code has less than 3 characters.  In that case it 
 does nothing.  It should work with both common channel codes like 
 BHZ and BHZ_01.  Note this function tacitly assumes a.arrival_chan_code is
 a single character like Z, N, or E.  
*/
string set_chan_this_phase(string chan, XcorAnalysisSetting& a)
{
	if(chan.length()<3)
		return(chan);
	else if(chan.length()==3)
	{
		string root(chan,0,2);
		return(root+a.arrival_chan_code);
	}
	else
	{
		string root(chan,0,2);
		string tail(chan.begin()+3,chan.end());
		return(root+a.arrival_chan_code+tail);
	}
}
void XcorProcessingEngine::save_results(int evid, int orid ,Hypocenter& h)
{
	// First save the beam
	int pwfid;
	string pchan(analysis_setting.component_name);
	string filter(analysis_setting.filter_param.type_description(true));
	string filter_param;
	if(save_extensions)
	{   try {
		int record;
		// First get and save the array beam
		TimeSeries beam=mcc->ArrayBeam();
		int fold=beam.get_int("fold");
		filter_param=beam.get_string("filter_spec");
		// Set dir and dfile
		beam.put("wfprocess.dir",beam_directory);
		beam.put("dir",beam_directory);
		beam.put("wfprocess.dfile",beam_dfile);
		beam.put("wfprocess.endtime",beam.endtime());
		record=dbsave(beam,dbwfprocess,string("wfprocess"),
				beam_mdl,am);
		dbwfprocess.record=record;
		dbgetv(dbwfprocess,0,"pwfid",&pwfid,0);
		double beam_amplitude=beam.get_double(beam_rms_key);
		//
		// For the present the chan code will be the same
		// as pchan
		string net;
		net=waveform_ensemble.get_string("netname");
		record=dbaddv(dbxcorbeam,0,"netname",net.c_str(),
			"chan",pchan.c_str(),
			"pchan",pchan.c_str(),
			"phase",analysis_setting.phase_for_analysis.c_str(),
			"pwfid",pwfid,
			"filter",filter_param.c_str(),
			"robustt0",analysis_setting.robust_tw.start,
			"robusttwin",analysis_setting.robust_tw.length(),
			"fold",fold,
			"amp",beam_amplitude,
				0);
		if(record<0)
			cerr << "save_results(Warning):  problems adding to xcorbeam table"<<endl;
		dbaddv(dbevlink,0,"evid",evid,"pwfid",pwfid,0);
	    }
	    catch (MetadataGetError mderr)
	    {
		cerr << "Problems getting attributes needed for xcorbeam"<<endl
			
			<< "Coding error or problem in MetadataList parameters"
			<< endl;
		mderr.log_error();
	    }
	    catch (...) 
	    {
		cerr << "Unexpected exception"<<endl;
		throw;
	    }
	}
		
	// I think static in this context means they are set once
	// and only once at startup
	static const string authbase("dbxcor");
	char username[20];
	my_username(username);
	string auth=authbase+":"+string(username);
	/* In dbxcor deltim is posted to the ensemble metadata.
	// for safety we need a default since that is a bit of an
	// odd mechanism to do this.  Due to the interface definition
	// we are locked into the use of a try-catch construct here.
	// Evil, but the only choice.  A SERIOUS maintenance issue
	// is also the use of a fixed null value used if the deltim
	// value is not set.  Here we use the value defined in 
	// Antelope 4.9.  If this changes, problems could surface.
	*/
	const double deltimnull(-1.0);
	double deltim;
	try {
		deltim=waveform_ensemble.get_double("deltim");
	} catch (...) {deltim=deltimnull;};
	// Since this is hidden behind the interface I'm going
	// to use the standard datascope API instead of going
	// through the DatascopeHandle API.  Since this code
	// is locked into Datascope anyway, this should not be
	// an issue.  It should execute faster without the additional
	// overhead of another layer of software.
	// Save arrival and assoc trace by trace.
	//
	vector<TimeSeries>::iterator trace;
	int i;
	for(i=0, trace=waveform_ensemble.member.begin();
		trace!=waveform_ensemble.member.end();++trace,++i)
	{
		string sta;
		string chan;
		string phase;
		double atime,predtime,resid;
		double lag;
		double xcorpeak,coh,stack_weight,amplitude;
		double gain;
		int record;
		int arid;
		// Skip data marked dead
		if(trace->live)
		{
		    try {
			lag=trace->get_double(moveout_keyword);
			//
			// Skip data marked with indeterminate moveout
			// This is used by MultichannelCorrelator to 
			// flag data it could not handle
			//
			if(fabs(lag)>MoveoutBadTest) continue;
			sta=trace->get_string("sta");
			chan=trace->get_string("chan");
			atime=trace->get_double(arrival_time_key);
			predtime=trace->get_double(predicted_time_key);
			resid=atime-predtime;
			xcorpeak=trace->get_double(peakxcor_keyword);
			coh=trace->get_double(coherence_keyword);
			stack_weight=trace->get_double(stack_weight_keyword);
			amplitude=trace->get_double(amplitude_static_keyword);
			// We accumulate amplitude changes in gain so we
			// need to get the final amplitude adjustment using
			// the gain attribute
			gain=trace->get_double(gain_keyword);
			amplitude *=gain;
			/* Need to repost snr so it can be saved consistently
			with css3.0 naming convention.  A bit awkward, but
			stuck with this.  */
			double snrtmp=trace->get_double(snr_keyword);
			trace->put("arrival.snr",snrtmp);
			// Write nothing for events that don't satisfy
			// all of the criteria on xcor, coherence, or weight
			if( (xcorpeak>xcorpeak_cutoff)
				&& (coh>coherence_cutoff)
				&& (stack_weight>stack_weight_cutoff) )
			{
			    if(save_extensions)
			    {
			      filter_param=trace->get_string("filter_spec");
			      record=dbaddv(dbxcorarrival,0,"sta",sta.c_str(),
					"chan",chan.c_str(),
					"phase",analysis_setting.phase_for_analysis.c_str(),
					"pwfid",pwfid,
					"filter",filter_param.c_str(),
					"algorithm",auth.c_str(),
					"pchan",pchan.c_str(),
					"time",atime,
					"twin",analysis_setting.analysis_tw.length(),
					"samprate",1.0/(trace->dt),
					"stackwgt",stack_weight,
					"coherence",coh,
					"relamp",amplitude,
					"xcorpeak",xcorpeak,0);
			      if(record<0)
			      {
				cerr << "save_results(warning):  problems saving xcorarrival table"
					<<endl;
			      }
			    }
			    /* Do not save arrival/assoc if subarray mode is enabled.  In that
			    situation there is no definitive arrival for a given station and
			    inconsistencies must be resolved by a least squares procedure */
			    if(use_subarrays) continue;

			    // These need to be computed and posted to
			    // metadata for this trace object before
			    // we attempt to update the database
			    double stalat,stalon;
			    stalat=rad(trace->get_double("lat"));
			    stalon=rad(trace->get_double("lon"));
			    double delta,seaz,esaz;
                            delta=h.distance(stalat,stalon);
                            seaz=h.seaz(stalat,stalon);
                            esaz=h.esaz(stalat,stalon);
			    /* A better solution is needed in dbpp to
			    avoid this kind of thing, but for now it
			    is necessary */
			    string statmp,chantmp;
			    statmp=trace->get_string("sta");
			    chantmp=trace->get_string("chan");
			    // This routine replaces chantmp with a
			    // channel code defined for this phase from
			    // analysis_setting.  
			    chantmp=set_chan_this_phase(chantmp,analysis_setting);
			    // These need to be posted as match keys
			    trace->put("evid",evid);
			    trace->put("orid",orid);
			    trace->put("phase",
				analysis_setting.phase_for_analysis);
			    trace->put("assoc.sta",statmp);
			    trace->put("arrival.sta",statmp);
			    trace->put("arrival.chan",chantmp);
			    trace->put("assoc.delta",deg(delta));
			    trace->put("assoc.seaz",deg(seaz));
			    trace->put("assoc.esaz",deg(esaz));
			    // We need to post these too
			    trace->put("assoc.timeres",resid);
			    trace->put("assoc.timedef",string("d"));
			    trace->put("assoc.phase",
				 analysis_setting.phase_for_analysis);
			    trace->put("assoc.vmodel",h.tt_definition());
			    // Not really needed, but better to post this
			    // for long term utility
			    trace->put("assoc.orid",orid);
			    trace->put("arrival.time",atime);
			    trace->put("arrival.iphase",
				analysis_setting.phase_for_analysis);
			    trace->put("arrival.auth",auth);
			    trace->put("arrival.jdate",yearday(atime));
			    trace->put("arrival.deltim",deltim);
			    try {
			    	int arruerr=arru.update(*trace);
			        if(arruerr)
			        {
				  cerr << "XcorProcessingEngine (WARNING):  "
					<< "ArrivalUpdater encountered "
					<< arruerr << "problems in saving results"<<endl;
				  cerr << "Turn on verbose for more details"<<endl;
			        }
			    }
			    catch (SeisppError serr)
			    {
				serr.log_error();
				exit(-1);
			    }
			}
		    }
		    catch (MetadataGetError mderr)
		    {
			cerr << "save_results:  problem with trace number "
				<< i << endl;
			mderr.log_error();
		    }				
		}
	}
}	
// These are private functions hidden by the interface
void XcorProcessingEngine::UpdateGeometry(TimeWindow twin)
{
	if(stations.GeometryIsValid(twin))
	{
		return;
	}
	else
	{
		stations=SeismicArray(dynamic_cast<DatabaseHandle&>(waveform_db_handle),
			twin.start,netname);
		load_subarrays_from_pf(stations,pf_used_by_engine);
	}
	if(stations.array.size()<=0)
		throw SeisppError(string("XcorProcessingEngine::UpdateGeometry -- network name=")
			+ netname
			+ string(" has no active stations at current time.") );
}
// this applies time shift set by pick_beam;
void XcorProcessingEngine::shift_arrivals(double tshift)
{
	vector<TimeSeries>::iterator trace;
	double moveout,atime;
	for(trace=waveform_ensemble.member.begin();
                trace!=waveform_ensemble.member.end();++trace)
	{
		//
		// Intentionally not putting a try block here to 
		// catch an exception for moveout not being defined.
		// Shouldn't get here in that condition so I'm
		// intentionally avoiding the overhead of a try block
		// glp 1/13/2006
		//
		try {
		  if(trace->live)
		  {
			moveout=trace->get_double(moveout_keyword);
			atime=trace->get_double(arrival_time_key);
			if(fabs(moveout)<MoveoutBadTest)
			{
				moveout+=tshift;
				atime+=tshift;
				trace->put(moveout_keyword,moveout);
				trace->put(arrival_time_key,atime);
			}
		  } 
		} catch (MetadataGetError mde) {
		    mde.log_error();
		}
	}
}
	
void XcorProcessingEngine::restore_original_ensemble()
{
	if(use_subarrays)
	{
		SeismicArray subnet=stations.subset(current_subarray);
		auto_ptr<TimeSeriesEnsemble> csub(ArraySubset(*regular_gather,subnet));
		waveform_ensemble=*csub;
	}
	else
	{
		waveform_ensemble=*regular_gather;
	}
	FilterEnsemble(waveform_ensemble,analysis_setting.filter_param);
	if(autoscale_initial)
	{
		MeasureEnsemblePeakAmplitudes<TimeSeriesEnsemble,TimeSeries>
			(waveform_ensemble,gain_keyword);
		ScaleEnsemble<TimeSeriesEnsemble,TimeSeries>
			(waveform_ensemble,gain_keyword,true);
		ScaleCalib<TimeSeriesEnsemble>
			(waveform_ensemble,gain_keyword,amplitude_static_keyword); 
	}
	else
		InitializeEnsembleAttribute<TimeSeriesEnsemble,double>
			(waveform_ensemble,gain_keyword,(double)1.0);
}
void XcorProcessingEngine::next_subarray()
{
	// Note this method intentionally does nothing if use_subarrays is false.
	if(use_subarrays)
	{
	    try{
		++current_subarray;
		SeismicArray ss=stations.subset(current_subarray);
		current_subarray_name=ss.name;
		auto_ptr<TimeSeriesEnsemble> csub(ArraySubset(*regular_gather,ss));
		waveform_ensemble=*csub;
		FilterEnsemble(waveform_ensemble,analysis_setting.filter_param);
		if(autoscale_initial)
		{
		    MeasureEnsemblePeakAmplitudes<TimeSeriesEnsemble,TimeSeries>
			(waveform_ensemble,gain_keyword);
		    ScaleEnsemble<TimeSeriesEnsemble,TimeSeries>
			(waveform_ensemble,gain_keyword,true);
		    ScaleCalib<TimeSeriesEnsemble>
			(waveform_ensemble,gain_keyword,amplitude_static_keyword); 
		}
		else
			InitializeEnsembleAttribute<TimeSeriesEnsemble,double>
				(waveform_ensemble,gain_keyword,(double)1.0);
		    
	    } catch (...) {throw;}
	}
	else
	    throw SeisppError(string("XcorProcessingEngine::next_subarray():  ")
			+ string("usage error.  ")
			+ string("This method should be called only when using subarrays"));
}
int XcorProcessingEngine::number_subarrays()
{
	return(stations.number_subarrays());
}

