/**
 * @file   TrajCluster_module.cc
 * @brief  Cluster finder using trajectories
 * @author Bruce Baller (baller@fnal.gov)
 * 
*
 */


// C/C++ standard libraries
#include <string>
#include <utility> // std::unique_ptr<>

// Framework libraries
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "canvas/Utilities/InputTag.h"
#include "art/Framework/Services/Optional/TFileService.h"

//LArSoft includes
#include "larreco/RecoAlg/TrajClusterAlg.h"
#include "larreco/RecoAlg/TCAlg/DataStructs.h"
#include "lardataobj/RecoBase/SpacePoint.h"
#include "lardataobj/AnalysisBase/CosmicTag.h"
#include "larsim/MCCheater/BackTrackerService.h"
#include "larsim/MCCheater/ParticleInventoryService.h"
#include "lardataobj/AnalysisBase/BackTrackerMatchingData.h"

//root includes
#include "TTree.h"

// ... more includes in the implementation section

namespace cluster {
  /**
   * @brief Produces clusters by the TrajCluster algorithm
   * 
   * Configuration parameters
   * -------------------------
   * 
   * - *HitFinderModuleLabel* (InputTag, mandatory): label of the hits to be
   *   used as input (usually the label of the producing module is enough)
   * - *TrajClusterAlg* (parameter set, mandatory): full configuration for
   *   TrajClusterAlg algorithm
   * 
   */
  class TrajCluster: public art::EDProducer {
    
  public:
    explicit TrajCluster(fhicl::ParameterSet const & pset);
    
    void reconfigure(fhicl::ParameterSet const & pset) ;
    void produce(art::Event & evt) override;
    void beginJob() override;
    void endJob() override;
    
  private:

    std::unique_ptr<tca::TrajClusterAlg> fTCAlg; // define TrajClusterAlg object
    TTree* showertree;
//    TTree* crtree;

    art::InputTag fHitModuleLabel;
    art::InputTag fSliceModuleLabel;
    art::InputTag fHitTruthModuleLabel;
    
    bool fDoWireAssns;
    bool fDoRawDigitAssns;
    
  }; // class TrajCluster
  
} // namespace cluster

//******************************************************************************
//*** implementation
//***

// C/C++ standard libraries
#include <vector>
#include <memory> // std::move()

// Framework libraries
#include "canvas/Utilities/Exception.h"
#include "art/Framework/Principal/Handle.h"
#include "canvas/Persistency/Common/Assns.h"
#include "canvas/Persistency/Common/FindManyP.h"

//LArSoft includes
#include "larcoreobj/SimpleTypesAndConstants/geo_types.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "lardata/ArtDataHelper/HitCreator.h" // recob::HitCollectionAssociator
#include "lardataobj/RecoBase/Slice.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/EndPoint2D.h"
#include "lardataobj/RecoBase/Vertex.h"
#include "lardataobj/RecoBase/Shower.h"


namespace cluster {
  
  struct HitLoc {
    unsigned int index; // index of this entry in a sort vector
    unsigned int ctp;   // encoded Cryostat, TPC and Plane
    unsigned int wire;  
    int tick;           // hit StartTick using typedef int TDCtick_t in RawTypes.h
    short localIndex;   // defined in Hit.h
  };

  //----------------------------------------------------------------------------
  bool SortHits(HitLoc const& h1, HitLoc const& h2)
  {
    // sort by hit location (Cryostat, TPC, Plane, Wire, StartTick, hit LocalIndex)
    if(h1.ctp != h2.ctp) return h1.ctp < h2.ctp;
    if(h1.wire != h2.wire) return h1.wire < h2.wire;
    if(h1.tick != h2.tick) return h1.tick < h2.tick;
    return h1.localIndex < h2.localIndex;
  } // SortHits

  //----------------------------------------------------------------------------
  void TrajCluster::reconfigure(fhicl::ParameterSet const & pset)
  {
    // this trick avoids double configuration on construction
    if (fTCAlg)
      fTCAlg->reconfigure(pset.get< fhicl::ParameterSet >("TrajClusterAlg"));
    else {
      fTCAlg.reset(new tca::TrajClusterAlg(pset.get< fhicl::ParameterSet >("TrajClusterAlg")));
    }


    fHitModuleLabel = "NA";
    if(pset.has_key("HitModuleLabel")) fHitModuleLabel = pset.get<art::InputTag>("HitModuleLabel");
    fSliceModuleLabel = "NA";
    if(pset.has_key("SliceModuleLabel")) fSliceModuleLabel = pset.get<art::InputTag>("SliceModuleLabel");
    fHitTruthModuleLabel = "NA";
    if(pset.has_key("HitTruthModuleLabel")) fHitTruthModuleLabel = pset.get<art::InputTag>("HitTruthModuleLabel");
    fDoWireAssns = pset.get<bool>("DoWireAssns",true);
    fDoRawDigitAssns = pset.get<bool>("DoRawDigitAssns",true);

  } // TrajCluster::reconfigure()
  
  //----------------------------------------------------------------------------
  TrajCluster::TrajCluster(fhicl::ParameterSet const& pset) {
    
    reconfigure(pset);
    
    // let HitCollectionAssociator declare that we are going to produce
    // hits and associations with wires and raw digits
    // (with no particular product label)
    recob::HitCollectionAssociator::declare_products(*this,"",fDoWireAssns,fDoRawDigitAssns);
    
    produces< std::vector<recob::Cluster> >();
    produces< std::vector<recob::Vertex> >();
    produces< std::vector<recob::EndPoint2D> >();
    produces< std::vector<recob::Shower> >();
    produces< art::Assns<recob::Cluster, recob::Hit> >();
    produces< art::Assns<recob::Cluster, recob::EndPoint2D, unsigned short> >();
    produces< art::Assns<recob::Cluster, recob::Vertex, unsigned short> >();
    produces< art::Assns<recob::Shower, recob::Hit> >();
    
    produces< std::vector<recob::PFParticle> >();
    produces< art::Assns<recob::PFParticle, recob::Cluster> >();
    produces< art::Assns<recob::PFParticle, recob::Shower> >();
    produces< art::Assns<recob::PFParticle, recob::Vertex> >();
    
    produces< art::Assns<recob::Slice, recob::PFParticle> >();
    produces< art::Assns<recob::Slice, recob::Hit> >();

    produces< std::vector<anab::CosmicTag>>();
    produces< art::Assns<recob::PFParticle, anab::CosmicTag>>();
  } // TrajCluster::TrajCluster()

  //----------------------------------------------------------------------------
  void TrajCluster::beginJob()
  {
    art::ServiceHandle<art::TFileService> tfs;

    showertree = tfs->make<TTree>("showervarstree", "showerVarsTree");
    fTCAlg->DefineShTree(showertree);
//    crtree = tfs->make<TTree>("crtree", "Cosmic removal variables");
//    fTCAlg->DefineCRTree(crtree);
  }
  
  //----------------------------------------------------------------------------
  void TrajCluster::endJob()
  {
    std::vector<unsigned int> const& fAlgModCount = fTCAlg->GetAlgModCount();
    std::vector<std::string> const& fAlgBitNames = fTCAlg->GetAlgBitNames();
    if(fAlgBitNames.size() != fAlgModCount.size()) return;
    mf::LogVerbatim myprt("TC");
    myprt<<"TrajCluster algorithm counts\n";
    unsigned short icol = 0;
    for(unsigned short ib = 0; ib < fAlgModCount.size(); ++ib) {
      if(ib == tca::kKilled) continue;
      myprt<<std::left<<std::setw(18)<<fAlgBitNames[ib]<<std::right<<std::setw(10)<<fAlgModCount[ib]<<" ";
      ++icol;
      if(icol == 4) { myprt<<"\n"; icol = 0; }
    } // ib
  } // endJob
  
  //----------------------------------------------------------------------------
  void TrajCluster::produce(art::Event & evt)
  {
    // Get a single hit collection from a HitsModuleLabel or multiple sets of "sliced" hits
    // (aka clusters of hits that are close to each other in 3D) from a SliceModuleLabel. 
    // A pointer to the full hit collection is passed to TrajClusterAlg. The hits that are 
    // in each slice are tracked to find 2D trajectories (that become clusters), 
    // 2D vertices (EndPoint2D), 3D vertices, PFParticles and Showers. These data products
    // are then collected and written to the event. Each slice is considered as an independent
    // collection of hits with the additional requirement that all hits in a slice reside in
    // one TPC
    
    // Define a vector of indices into inputHits (= evt.allHits in TrajClusterAlg) 
    // for each slice for hits associated with 3D-clustered SpacePoints
    std::vector<std::vector<unsigned int>> slHitsVec;
    // Slice IDs that will be correlated with sub-slices
    std::vector<unsigned short> slcIDs;
    // pointers to the slices in the event
    std::vector<art::Ptr<recob::Slice>> slices;
    unsigned int nInputHits = 0;
    // get a handle for the hit collection
    auto inputHits = art::Handle<std::vector<recob::Hit>>();
    if(!evt.getByLabel(fHitModuleLabel, inputHits)) {
      std::cout<<"Failed to get a hits handle\n";
      return;
    }
    // This is a pointer to a vector of recob::Hits that exist in the event. The hits
    // are not copied.
    if(!fTCAlg->SetInputHits(*inputHits)) throw cet::exception("TrajClusterModule")<<"Failed to process hits from '"<<fHitModuleLabel.label()<<"'\n";
    nInputHits = (*inputHits).size();
    if(fSliceModuleLabel != "NA") {
      // Expecting to find sliced hits from Slice -> Hits assns
      auto slcHandle = evt.getValidHandle<std::vector<recob::Slice>>(fSliceModuleLabel);
      art::fill_ptr_vector(slices, slcHandle);
      art::FindManyP<recob::Hit> hitFromSlc(slcHandle, evt, fSliceModuleLabel);
      for(size_t isl = 0; isl < slices.size(); ++isl) {
        auto& hit_in_slc = hitFromSlc.at(isl);
        if(hit_in_slc.size() < 3) continue;
        std::vector<unsigned int> slhits(hit_in_slc.size());
        unsigned int indx = 0;
        if(hit_in_slc[0].id() != inputHits.id()) throw cet::exception("TrajClusterModule")<<"Input hits from '"<<fHitModuleLabel.label()<<"' have a different product id than hits referenced in '"<<fSliceModuleLabel.label()<<"'\n";
        for(auto& hit : hit_in_slc) {
          if(hit.key() > nInputHits - 1) throw cet::exception("TrajClusterModule")<<"Found an invalid slice index "<<hit.key()<<" to the input hit collection of size "<<nInputHits<<"\n";
          slhits[indx] = hit.key();
          ++indx;
        } // hit
        if(slhits.size() < 3) continue;
        slHitsVec.push_back(slhits);
        slcIDs.push_back(slices[isl]->ID());
      } // isl
    } else {
      // There was no pre-processing of the hits to define logical slices
      // so put all hits in one slice
      slHitsVec.resize(1);
      slHitsVec[0].resize(nInputHits);
      for(unsigned int iht = 0; iht < nInputHits; ++iht) slHitsVec[0][iht] = iht;
      slcIDs.resize(1, 1);
    } // no input slices
    
    // split slHitsVec so that all hits in a sub-slice are in the same TPC
    const geo::GeometryCore* geom = lar::providerFrom<geo::Geometry>();
    if(geom->NTPC() > 1) {
      std::vector<std::vector<unsigned int>> tpcSlcHitsVec;
      std::vector<unsigned short> tpcSlcIDs;
      for(unsigned short isl = 0; isl < slHitsVec.size(); ++isl) {
        auto& slhits = slHitsVec[isl];
        if(slhits.size() < 2) continue;
        // list of hits in this slice in each TPC
        std::vector<std::vector<unsigned int>> tpcHits;
        // list of TPCs in this slice
        std::vector<unsigned short> tpcNum;
        for(auto iht : slhits) {
          auto& hit = (*inputHits)[iht];
          unsigned short tpc = hit.WireID().TPC;
          unsigned short tpcIndex = 0;
          for(tpcIndex = 0; tpcIndex < tpcNum.size(); ++tpcIndex) if(tpcNum[tpcIndex] == tpc) break;
          if(tpcIndex == tpcNum.size()) {
            // not in tpcNum so make a new entry
            tpcHits.resize(tpcIndex + 1);
            tpcNum.push_back(tpc);
          }
          tpcHits[tpcIndex].push_back(iht);
        } // iht
        for(auto& tHits : tpcHits) {
          tpcSlcHitsVec.push_back(tHits);
          tpcSlcIDs.push_back(slcIDs[isl]);
        }
      } // slhits
      // over-write slHitsVec
      slHitsVec = tpcSlcHitsVec;
      slcIDs = tpcSlcIDs;
    } // > 1 TPC
    
    // First sort the hits in each slice and then reconstruct
    for(unsigned short isl = 0; isl < slHitsVec.size(); ++isl) {
      auto& slhits = slHitsVec[isl];
      // sort the slice hits by Cryostat, TPC, Wire, Plane, Start Tick and LocalIndex.
      // This assumes that hits with larger LocalIndex are at larger Tick.
      std::vector<HitLoc> sortVec(slhits.size());
      bool badHit = false;
      for(unsigned short indx = 0; indx < slhits.size(); ++indx) {
        if(slhits[indx] > nInputHits - 1) {
          badHit = true;
          break;
        }
        auto& hit = (*inputHits)[slhits[indx]];
        sortVec[indx].index = indx;
        sortVec[indx].ctp = tca::EncodeCTP(hit.WireID());
        sortVec[indx].wire = hit.WireID().Wire;
        sortVec[indx].tick = hit.StartTick();
        sortVec[indx].localIndex = hit.LocalIndex();
      } // iht
      if(badHit) {
        std::cout<<"TrajCluster found an invalid slice reference to the input hit collection. Ignoring this slice.\n";
        continue;
      }
      std::sort(sortVec.begin(), sortVec.end(), SortHits);
      // make a temporary copy of slhits
      auto tmp = slhits;
      // put slhits into proper order
      for(unsigned short ii = 0; ii < slhits.size(); ++ii) slhits[ii] = tmp[sortVec[ii].index];
      // clear the temp vector
      tmp.resize(0);
      // reconstruct using the hits in this slice. The data products are stored internally in
      // TrajCluster data structs.
      fTCAlg->RunTrajClusterAlg(slhits);
    } // isl

    if(!evt.isRealData() && tca::tcc.matchTruth[0] >= 0 && fHitTruthModuleLabel != "NA") {
      // TODO: Add a check here to ensure that a neutrino vertex exists inside any TPC
      // when checking neutrino reconstruction performance.
      // create a list of MCParticles of interest
      std::vector<simb::MCParticle*> mcpList;
      // and a vector of MC-matched hits
      std::vector<unsigned int> mcpListIndex((*inputHits).size(), UINT_MAX);
      // save MCParticles that have the desired MCTruth origin using
      // the Origin_t typedef enum: kUnknown, kBeamNeutrino, kCosmicRay, kSuperNovaNeutrino, kSingleParticle
      simb::Origin_t origin = (simb::Origin_t)tca::tcc.matchTruth[0];
      // or save them all
      bool anySource = (origin == simb::kUnknown);
      // get the assns
      art::FindManyP<simb::MCParticle,anab::BackTrackerHitMatchingData> particles_per_hit(inputHits, evt, fHitTruthModuleLabel);
      art::ServiceHandle<cheat::ParticleInventoryService> pi_serv;
      sim::ParticleList const& plist = pi_serv->ParticleList();
      for(sim::ParticleList::const_iterator ipart = plist.begin(); ipart != plist.end(); ++ipart) {
        auto& p = (*ipart).second;
        int trackID = p->TrackId();
        art::Ptr<simb::MCTruth> theTruth = pi_serv->TrackIdToMCTruth_P(trackID);
        int KE = 1000 * (p->E() - p->Mass());
        if(!anySource && theTruth->Origin() != origin) continue;
        if(tca::tcc.matchTruth[1] > 1 && KE > 10) {
          std::cout<<"TCM: mcp Origin "<<theTruth->Origin()
          <<std::setw(8)<<p->TrackId()
          <<" pdg "<<p->PdgCode()
          <<std::setw(7)<<KE<<" mom "<<p->Mother()
          <<" "<<p->Process()
          <<"\n";
        }
        mcpList.push_back(p);
      } // ipart
      if(!mcpList.empty()) {
        std::vector<art::Ptr<simb::MCParticle>> particle_vec;
        std::vector<anab::BackTrackerHitMatchingData const*> match_vec;
        unsigned int nMatHits = 0;
        for(unsigned int iht = 0; iht < (*inputHits).size(); ++iht) {
          particle_vec.clear(); match_vec.clear();
          try{ particles_per_hit.get(iht, particle_vec, match_vec); }
          catch(...) {
            std::cout<<"BackTrackerHitMatchingData not found\n";
            break;
          }
          if(particle_vec.empty()) continue;
          int trackID = 0;
          for(unsigned short im = 0; im < match_vec.size(); ++im) {
            if(match_vec[im]->ideFraction < 0.5) continue;
            trackID = particle_vec[im]->TrackId();
            break;
          } // im
          if(trackID == 0) continue;
          // look for this in MCPartList
          for(unsigned int ipart = 0; ipart < mcpList.size(); ++ipart) {
            auto& mcp = mcpList[ipart];
            if(mcp->TrackId() != trackID) continue;
            mcpListIndex[iht] = ipart;
            ++nMatHits;
            break;
          } // ipart
        } // iht
        if(tca::tcc.matchTruth[1] > 1) std::cout<<"Loaded "<<mcpList.size()<<" MCParticles. "<<nMatHits<<"/"<<(*inputHits).size()<<" hits are matched to MCParticles\n";
        fTCAlg->fTM.MatchTruth(mcpList, mcpListIndex);
        if(tca::tcc.matchTruth[0] >= 0) fTCAlg->fTM.PrintResults(evt.event());
      } // mcpList not empty
    } // match truth
    
    if(tca::tcc.dbgSummary) tca::PrintAll("TCM");

    // Vectors to hold all data products that will go into the event
    std::vector<recob::Hit> hitCol;       // output hit collection
    std::vector<recob::Cluster> clsCol;
    std::vector<recob::PFParticle> pfpCol;
    std::vector<recob::Vertex> vx3Col;
    std::vector<recob::EndPoint2D> vx2Col;
    std::vector<recob::Shower> shwCol;
    std::vector<anab::CosmicTag> ctCol;
    // a vector to correlate inputHits with hitCol
    std::vector<unsigned int> newIndex(nInputHits, UINT_MAX);
    
    // assns for those data products
    // Cluster -> ...
    std::unique_ptr<art::Assns<recob::Cluster, recob::Hit>> 
      cls_hit_assn(new art::Assns<recob::Cluster, recob::Hit>);
    // unsigned short is the end to which a vertex is attached
    std::unique_ptr<art::Assns<recob::Cluster, recob::EndPoint2D, unsigned short>>  
      cls_vx2_assn(new art::Assns<recob::Cluster, recob::EndPoint2D, unsigned short>);
    std::unique_ptr<art::Assns<recob::Cluster, recob::Vertex, unsigned short>>  
      cls_vx3_assn(new art::Assns<recob::Cluster, recob::Vertex, unsigned short>);
    // Shower -> ...
    std::unique_ptr<art::Assns<recob::Shower, recob::Hit>>
      shwr_hit_assn(new art::Assns<recob::Shower, recob::Hit>);
    // PFParticle -> ...
    std::unique_ptr<art::Assns<recob::PFParticle, recob::Cluster>>
      pfp_cls_assn(new art::Assns<recob::PFParticle, recob::Cluster>);
    std::unique_ptr<art::Assns<recob::PFParticle, recob::Shower>>
      pfp_shwr_assn(new art::Assns<recob::PFParticle, recob::Shower>);
    std::unique_ptr<art::Assns<recob::PFParticle, recob::Vertex>> 
      pfp_vx3_assn(new art::Assns<recob::PFParticle, recob::Vertex>);
    std::unique_ptr<art::Assns<recob::PFParticle, anab::CosmicTag>>
      pfp_cos_assn(new art::Assns<recob::PFParticle, anab::CosmicTag>);
    // Slice -> ...
    std::unique_ptr<art::Assns<recob::Slice, recob::PFParticle>>
      slc_pfp_assn(new art::Assns<recob::Slice, recob::PFParticle>);
    std::unique_ptr<art::Assns<recob::Slice, recob::Hit>>
      slc_hit_assn(new art::Assns<recob::Slice, recob::Hit>);

    unsigned short nSlices = fTCAlg->GetSlicesSize();
    // define a hit collection begin index to pass to CreateAssn for each cluster
    unsigned int hitColBeginIndex = 0;
    for(unsigned short isl = 0; isl < nSlices; ++isl) {
      unsigned short slcIndex = 0;
      if(!slices.empty()) {
        for(slcIndex = 0; slcIndex < slices.size(); ++slcIndex) if(slices[slcIndex]->ID() != slcIDs[isl]) break;
        if(slcIndex == slices.size()) continue;
      }
      auto& slc = fTCAlg->GetSlice(isl);
      // See if there was a serious reconstruction failure that made the slice invalid
      if(!slc.isValid) continue;
      // make EndPoint2Ds
      for(auto& vx2 : slc.vtxs) {
        if(vx2.ID <= 0) continue;
        unsigned int vtxID = vx2.UID;
        unsigned int wire = std::nearbyint(vx2.Pos[0]);
        geo::PlaneID plID = tca::DecodeCTP(vx2.CTP);
        geo::WireID wID = geo::WireID(plID.Cryostat, plID.TPC, plID.Plane, wire);
        geo::View_t view = tca::tcc.geom->View(wID);
        vx2Col.emplace_back((double)vx2.Pos[1]/tca::tcc.unitsPerTick,  // Time
                            wID,                  // WireID
                            vx2.Score,            // strength = score
                            vtxID,                // ID
                            view,                 // View
                            0);                   // total charge - not relevant
      } // vx2
      // make Vertices
      for(auto& vx3 : slc.vtx3s) {
        if(vx3.ID <= 0) continue;
        // ignore incomplete vertices
        if(vx3.Wire >= 0) continue;
        unsigned int vtxID = vx3.UID;
        double xyz[3];
        xyz[0] = vx3.X;
        xyz[1] = vx3.Y;
        xyz[2] = vx3.Z;
        vx3Col.emplace_back(xyz, vtxID);
      } // vx3
      // Convert the tjs to clusters
      bool badSlice = false;
      for(auto& tj : slc.tjs) {
        if(tj.AlgMod[tca::kKilled]) continue;
        float sumChg = 0;
        float sumADC = 0;
        hitColBeginIndex = hitCol.size();
        for(auto& tp : tj.Pts) {
          if(tp.Chg <= 0) continue;
          // index of inputHits indices  of hits used in one TP
          std::vector<unsigned int> tpHits;
          for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
            if(!tp.UseHit[ii]) continue;
            if(tp.Hits[ii] > slc.slHits.size() - 1) {
              badSlice = true;
              break;
            } // bad slHits index
            unsigned int allHitsIndex = slc.slHits[tp.Hits[ii]].allHitsIndex;
            if(allHitsIndex > nInputHits - 1) {
              badSlice = true;
              break;
            } // bad allHitsIndex
            tpHits.push_back(allHitsIndex);
            if(newIndex[allHitsIndex] != UINT_MAX) {
              std::cout<<"Bad Slice "<<isl<<" tp.Hits "<<tp.Hits[ii]<<" allHitsIndex "<<allHitsIndex;
              std::cout<<" old newIndex "<<newIndex[allHitsIndex];
              auto& oldhit = (*inputHits)[allHitsIndex];
              std::cout<<" old "<<oldhit.WireID().Plane<<":"<<oldhit.WireID().Wire<<":"<<(int)oldhit.PeakTime();
              auto& newhit = hitCol[newIndex[allHitsIndex]];
              std::cout<<" new "<<newhit.WireID().Plane<<":"<<newhit.WireID().Wire<<":"<<(int)newhit.PeakTime();
              std::cout<<" hitCol size "<<hitCol.size();
              std::cout<<"\n";
              badSlice = true;
              break;
            }
            newIndex[allHitsIndex] = hitCol.size();
          } // ii
          if(badSlice) break;
          // Let the alg define the hit either by merging multiple hits or by a simple copy
          // of a single hit from inputHits
          recob::Hit newHit = fTCAlg->MergeTPHits(tpHits);
          if(newHit.Channel() == raw::InvalidChannelID) {
            std::cout<<"TrajCluster module failed merging hits\n";
            badSlice = true;
            break;
          } // MergeTPHits failed
          sumChg += newHit.Integral();
          sumADC += newHit.SummedADC();
          // add it to the new hits collection
          hitCol.push_back(newHit);
          // Slice -> Hit assn
          if(!util::CreateAssn(*this, evt, hitCol, slices[slcIndex], *slc_hit_assn))
          {
            throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate new Hit with Slice";
          } // exception
        } // tp
        if(badSlice) {
          std::cout<<"Bad slice. Need some error recovery code here\n";
          break;
        }
        geo::View_t view = hitCol[hitColBeginIndex].View();
        auto& firstTP = tj.Pts[tj.EndPt[0]];
        auto& lastTP = tj.Pts[tj.EndPt[1]];
        int clsID = tj.UID;
        if(tj.AlgMod[tca::kShowerLike]) clsID = -clsID;
        unsigned short nclhits = hitCol.size() - hitColBeginIndex + 1;
        clsCol.emplace_back(
                            firstTP.Pos[0],         // Start wire
                            0,                      // sigma start wire
                            firstTP.Pos[1]/tca::tcc.unitsPerTick,         // start tick
                            0,                      // sigma start tick
                            firstTP.AveChg,         // start charge
                            firstTP.Ang,            // start angle
                            0,                      // start opening angle (0 for line-like clusters)
                            lastTP.Pos[0],          // end wire
                            0,                      // sigma end wire
                            lastTP.Pos[1]/tca::tcc.unitsPerTick,           // end tick
                            0,                      // sigma end tick
                            lastTP.AveChg,          // end charge
                            lastTP.Ang,             // end angle
                            0,                      // end opening angle (0 for line-like clusters)
                            sumChg,                 // integral
                            0,                      // sigma integral
                            sumADC,                 // summed ADC
                            0,                      // sigma summed ADC
                            nclhits,                // n hits
                            0,                      // wires over hits
                            0,                      // width (0 for line-like clusters)
                            clsID,                  // ID from TrajClusterAlg
                            view,                   // view
                            tca::DecodeCTP(tj.CTP), // planeID
                            recob::Cluster::Sentry  // sentry
                           );
        if(!util::CreateAssn(*this, evt, clsCol, hitCol, *cls_hit_assn, hitColBeginIndex, hitCol.size()))
        {
          throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate hits with cluster ID "<<tj.UID;
        } // exception
        // Make cluster -> 2V and cluster -> 3V assns
        for(unsigned short end = 0; end < 2; ++end) {
          if(tj.VtxID[end] <= 0) continue;
          for(unsigned short vx2Index = 0; vx2Index < slc.vtxs.size(); ++vx2Index) {
            auto& vx2 = slc.vtxs[vx2Index];
            if(vx2.ID != tj.VtxID[end]) continue;
            if(!util::CreateAssnD(*this, evt, *cls_vx2_assn, clsCol.size() - 1, vx2Index, end))
            {
              throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate cluster "<<tj.UID<<" with EndPoint2D";
            } // exception
            if(vx2.Vx3ID > 0) {
              for(unsigned short vx3Index = 0; vx3Index < slc.vtx3s.size(); ++vx3Index) {
                auto& vx3 = slc.vtx3s[vx3Index];
                if(vx3.ID != vx2.Vx3ID) continue;
                if(!util::CreateAssnD(*this, evt, *cls_vx3_assn, clsCol.size() - 1, vx3Index, end))
                {
                  throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate cluster "<<tj.UID<<" with Vertex";
                } // exception
                break;
              } // vx3Index
            } // vx2.Vx3ID > 0
            break;
          } // vx2
        } // end
      } // tj (aka cluster)
      // make Showers
      for(auto& ss3 : slc.showers) {
        if(ss3.ID <= 0) continue;
        recob::Shower shower;
        shower.set_id(ss3.UID);
        shower.set_total_energy(ss3.Energy);
        shower.set_total_energy_err(ss3.EnergyErr);
        shower.set_total_MIPenergy(ss3.MIPEnergy);
        shower.set_total_MIPenergy_err(ss3.MIPEnergyErr);
        shower.set_total_best_plane(ss3.BestPlane);
        TVector3 dir = {ss3.Dir[0], ss3.Dir[1], ss3.Dir[2]};
        shower.set_direction(dir);
        TVector3 dirErr = {ss3.DirErr[0], ss3.DirErr[1], ss3.DirErr[2]};
        shower.set_direction_err(dirErr);
        TVector3 pos = {ss3.Start[0], ss3.Start[1], ss3.Start[2]};
        shower.set_start_point(pos);
        TVector3 posErr = {ss3.StartErr[0], ss3.StartErr[1], ss3.StartErr[2]};
        shower.set_start_point_err(posErr);
        shower.set_dedx(ss3.dEdx);
        shower.set_dedx_err(ss3.dEdxErr);
        shower.set_length(ss3.Len);
        shower.set_open_angle(ss3.OpenAngle);
        shwCol.push_back(shower);
        // make the shower - hit association
        std::vector<unsigned int> shwHits(ss3.Hits.size());
        for(unsigned int iht = 0; iht < ss3.Hits.size(); ++iht) shwHits[iht] = newIndex[iht];
        if(!util::CreateAssn(*this, evt, *shwr_hit_assn, shwCol.size()-1, shwHits.begin(), shwHits.end()))
        {
          throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate hits with Shower";
        } // exception
      } // ss3
      // make PFParticles
      for(size_t ipfp = 0; ipfp < slc.pfps.size(); ++ipfp) {
        auto& pfp = slc.pfps[ipfp];
        if(pfp.ID <= 0) continue;
        // parents and daughters are indexed within a slice so find the index offset in pfpCol
        size_t self = pfpCol.size();
        size_t offset = self - ipfp;
        size_t parentIndex = UINT_MAX;
        if(pfp.ParentID > 0) parentIndex = pfp.ParentID + offset - 1;
        std::vector<size_t> dtrIndices(pfp.DtrIDs.size());
        for(unsigned short idtr = 0; idtr < pfp.DtrIDs.size(); ++idtr) dtrIndices[idtr] = pfp.DtrIDs[idtr] + offset - 1;
/* check the assns
        if(pfp.ParentID > 0 || !pfp.DtrIDs.empty()) {
          std::cout<<isl<<" UID "<<pfp.UID<<" ID "<<pfp.ID<<" ParentID "<<pfp.ParentID<<" self "<<self<<" parentIndex "<<parentIndex<<"\n";
          for(unsigned short idtr = 0; idtr < pfp.DtrIDs.size(); ++idtr) {
            std::cout<<" dtr "<<pfp.DtrIDs[idtr]<<" index "<<dtrIndices[idtr]<<"\n";
          } // idtr
        } // check
*/
        pfpCol.emplace_back(pfp.PDGCode, self, parentIndex, dtrIndices);
        // PFParticle -> clusters
        std::vector<unsigned int> clsIndices;
        for(auto tjid : pfp.TjIDs) {
          unsigned int clsIndex = 0;
          int tjUID = slc.tjs[tjid - 1].UID;
          for(clsIndex = 0; clsIndex < clsCol.size(); ++clsIndex) if(abs(clsCol[clsIndex].ID()) == tjUID) break;
          if(clsIndex == clsCol.size()) {
            std::cout<<"TrajCluster module invalid pfp -> tj -> cluster index\n";
            continue;
          }
          clsIndices.push_back(clsIndex);
        } // tjid
        if(!util::CreateAssn(*this, evt, *pfp_cls_assn, pfpCol.size()-1, clsIndices.begin(), clsIndices.end()))
        {
          throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate clusters with PFParticle";
        } // exception
        // PFParticle -> Vertex
        if(pfp.Vx3ID[0] > 0) {
          for(unsigned short vx3Index = 0; vx3Index < slc.vtx3s.size(); ++vx3Index) {
            auto& vx3 = slc.vtx3s[vx3Index];
            if(vx3.ID != pfp.Vx3ID[0]) continue;
            std::vector<unsigned short> indx(1, vx3Index);
            if(!util::CreateAssn(*this, evt, *pfp_vx3_assn, pfpCol.size() - 1, indx.begin(), indx.end()))
            {
              throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate PFParticle "<<pfp.UID<<" with Vertex";
            } // exception
          } // vx3Index
        } // start vertex exists
        // PFParticle -> Slice
        if(!slices.empty()) {
          if(!util::CreateAssn(*this, evt, pfpCol, slices[slcIndex], *slc_pfp_assn))
          {
            throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate slice with PFParticle";
          } // exception
        } // slices exist

        // PFParticle -> Shower
        if(pfp.PDGCode == 1111) {
          std::vector<unsigned short> shwIndex(1, 0);
          for(auto& ss3 : slc.showers) {
            if(ss3.ID <= 0) continue;
            if(ss3.PFPIndex == ipfp) break;
            ++shwIndex[0];
          } // ss3
          if(shwIndex[0] < shwCol.size()) {
            if(!util::CreateAssn(*this, evt, *pfp_shwr_assn, pfpCol.size()-1, shwIndex.begin(), shwIndex.end()))
            {
              throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate shower with PFParticle";
            } // exception
          } // valid shwIndex
        } // pfp -> Shower
        // PFParticle cosmic tag
        if(tca::tcc.modes[tca::kTagCosmics]) {
          std::vector<float> tempPt1, tempPt2;
          tempPt1.push_back(-999);
          tempPt1.push_back(-999);
          tempPt1.push_back(-999);
          tempPt2.push_back(-999);
          tempPt2.push_back(-999);
          tempPt2.push_back(-999);
          ctCol.emplace_back(tempPt1, tempPt2, pfp.CosmicScore, anab::CosmicTagID_t::kNotTagged);
          if (!util::CreateAssn(*this, evt, pfpCol, ctCol, *pfp_cos_assn, ctCol.size()-1, ctCol.size())){
            throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate CosmicTag with PFParticle";
          }
        } // cosmic tag
      } // ipfp
    } // slice isl
    // add the hits that weren't used in any slice to hitCol
    if(!slices.empty()) {
      auto slcHandle = evt.getValidHandle<std::vector<recob::Slice>>(fSliceModuleLabel);
      art::FindManyP<recob::Hit> hitFromSlc(slcHandle, evt, fSliceModuleLabel);
      for(unsigned int allHitsIndex = 0; allHitsIndex < nInputHits; ++allHitsIndex) {
        if(newIndex[allHitsIndex] != UINT_MAX) continue;
        hitCol.push_back((*inputHits)[allHitsIndex]);
        // find out which slice it is in
        for(size_t isl = 0; isl < slices.size(); ++isl) {
          auto& hit_in_slc = hitFromSlc.at(isl);
          for(auto& hit : hit_in_slc) {
            if(hit.key() != allHitsIndex) continue;
            // Slice -> Hit assn
            if(!util::CreateAssn(*this, evt, hitCol, slices[isl], *slc_hit_assn, allHitsIndex))
            {
              throw art::Exception(art::errors::ProductRegistrationFailure)<<"Failed to associate old Hit with Slice";
            } // exception
            break;
          } // hit
        } // isl
      } // allHitsIndex
    } // slices exist
    
    // clear the slices vector
    fTCAlg->ClearResults();

    // convert vectors to unique_ptrs
    std::unique_ptr<std::vector<recob::Hit> > hcol(new std::vector<recob::Hit>(std::move(hitCol)));
    std::unique_ptr<std::vector<recob::Cluster> > ccol(new std::vector<recob::Cluster>(std::move(clsCol)));
    std::unique_ptr<std::vector<recob::EndPoint2D> > v2col(new std::vector<recob::EndPoint2D>(std::move(vx2Col)));
    std::unique_ptr<std::vector<recob::Vertex> > v3col(new std::vector<recob::Vertex>(std::move(vx3Col)));
    std::unique_ptr<std::vector<recob::PFParticle> > pcol(new std::vector<recob::PFParticle>(std::move(pfpCol)));
    std::unique_ptr<std::vector<recob::Shower> > scol(new std::vector<recob::Shower>(std::move(shwCol)));
    std::unique_ptr<std::vector<anab::CosmicTag>> ctgcol(new std::vector<anab::CosmicTag>(std::move(ctCol)));


    // move the cluster collection and the associations into the event:
    if(fHitModuleLabel != "NA") {
      recob::HitRefinerAssociator shcol(*this, evt, fHitModuleLabel, fDoWireAssns, fDoRawDigitAssns);
      shcol.use_hits(std::move(hcol));
      shcol.put_into(evt);
    } else {
      recob::HitRefinerAssociator shcol(*this, evt, fSliceModuleLabel, fDoWireAssns, fDoRawDigitAssns);
      shcol.use_hits(std::move(hcol));
      shcol.put_into(evt);
    }
    evt.put(std::move(ccol));
    evt.put(std::move(cls_hit_assn));
    evt.put(std::move(v2col));
    evt.put(std::move(v3col));
    evt.put(std::move(scol));
    evt.put(std::move(shwr_hit_assn));
    evt.put(std::move(cls_vx2_assn));
    evt.put(std::move(cls_vx3_assn));
    evt.put(std::move(pcol));
    evt.put(std::move(pfp_cls_assn));
    evt.put(std::move(pfp_shwr_assn));
    evt.put(std::move(pfp_vx3_assn));
    evt.put(std::move(slc_pfp_assn));
    evt.put(std::move(slc_hit_assn));
    evt.put(std::move(ctgcol));
    evt.put(std::move(pfp_cos_assn));
  } // TrajCluster::produce()
  
  //----------------------------------------------------------------------------
  DEFINE_ART_MODULE(TrajCluster)
  
} // namespace cluster

