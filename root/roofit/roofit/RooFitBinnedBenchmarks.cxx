#include "RooWorkspace.h"
#include "RooAddPdf.h"
#include "RooRealVar.h"
#include "RooMinimizer.h"
#include "TFile.h"
#include "TH1.h"
#include "TRandom.h"
#include "TError.h"
#include "RooStats/HistFactory/Measurement.h"
#include "RooStats/HistFactory/HistoToWorkspaceFactoryFast.h"
#include "RooStats/ModelConfig.h"
#include "RooLinkedListIter.h"
#include "RooRealSumPdf.h"

#include "benchmark/benchmark.h"

using namespace RooFit;
using namespace RooStats;
using namespace HistFactory;

void buildBinnedTest()
{
   Measurement meas("meas", "meas");
   meas.SetPOI("SignalStrength");
   meas.SetLumi(1.0);
   meas.SetLumiRelErr(0.10);
   meas.AddConstantParam("Lumi");
   Channel chan("Region0");
   auto Signal_Hist = new TH1F("Signal", "Signal", 10, 0, 10);
   auto Background_Hist = new TH1F("Background", "Background", 10, 0, 10);
   auto Data_Hist = new TH1F("Data", "Data", 10, 0, 10);
   int nbins = Signal_Hist->GetXaxis()->GetNbins();
   for (int bin = 1; bin <= nbins; ++bin) {
      for (int i = 0; i <= bin; ++i) {
         Signal_Hist->Fill(bin + 0.5);
         Data_Hist->Fill(bin + 0.5);
      }
      for (int i = 0; i <= 100; ++i) {
         Background_Hist->Fill(bin + 0.5);
         Data_Hist->Fill(bin + 0.5);
      }
   }
   chan.SetData(Data_Hist);
   Sample background("background");
   background.SetNormalizeByTheory(false);
   background.SetHisto(Background_Hist);
   Sample signal("signal");
   signal.SetNormalizeByTheory(false);
   signal.SetHisto(Signal_Hist);
   signal.AddNormFactor("SignalStrength", 1, 0, 3);
   chan.AddSample(background);
   chan.AddSample(signal);
   meas.AddChannel(chan);
   HistoToWorkspaceFactoryFast hist2workspace(meas);
   RooWorkspace *ws = hist2workspace.MakeSingleChannelModel(meas, chan);
   auto iter = ws->components().fwdIterator();
   RooAbsArg *arg;
   while ((arg = iter.next())) {
      if (arg->IsA() == RooRealSumPdf::Class()) {
         arg->setAttribute("BinnedLikelihood");
         std::cout << "component " << arg->GetName() << " is a binned likelihood" << std::endl;
      }
   }
   ws->SetName("BinnedWorkspace");
   ws->writeToFile("workspace.root");
}

Sample addVariations(Sample asample, int nnps, bool channel_crosstalk, int channel)
{
   for (int nuis = 0; nuis < nnps; ++nuis) {
      TRandom *R = new TRandom(channel * nuis / nnps);
      Double_t random = R->Rndm();
      double uncertainty_up = (1 + random) / sqrt(100);
      double uncertainty_down = (1 - random) / sqrt(100);
      std::cout << "in channel " << channel << "nuisance +/- [" << uncertainty_up << "," << uncertainty_down << "]"
                << std::endl;
      std::string nuis_name = "norm_uncertainty_" + std::to_string(nuis);
      if (!channel_crosstalk) {
         nuis_name = nuis_name + "_channel_" + std::to_string(channel);
      }
      asample.AddOverallSys(nuis_name, uncertainty_up, uncertainty_down);
   }
   return asample;
}

Channel makeChannel(int channel, int nbins, int nnps, bool channel_crosstalk)
{
   std::string channel_name = "Region" + std::to_string(channel);
   Channel chan(channel_name);
   auto Signal_Hist = new TH1F("Signal", "Signal", nbins, 0, nbins);
   auto Background_Hist = new TH1F("Background", "Background", nbins, 0, nbins);
   auto Data_Hist = new TH1F("Data", "Data", nbins, 0, nbins);
   for (Int_t bin = 1; bin <= nbins; ++bin) {
      for (Int_t i = 0; i <= bin; ++i) {
         Signal_Hist->Fill(bin + 0.5);
         Data_Hist->Fill(bin + 0.5);
      }
      for (Int_t i = 0; i <= 100; ++i) {
         Background_Hist->Fill(bin + 0.5);
         Data_Hist->Fill(bin + 0.5);
      }
   }
   chan.SetData(Data_Hist);
   Sample background("background");
   background.SetNormalizeByTheory(false);
   background.SetHisto(Background_Hist);
   background.ActivateStatError();
   Sample signal("signal");
   signal.SetNormalizeByTheory(false);
   signal.SetHisto(Signal_Hist);
   signal.ActivateStatError();
   signal.AddNormFactor("SignalStrength", 1, 0, 3);
   if (nnps > 0) {
      signal = addVariations(signal, nnps, true, channel);
      background = addVariations(background, nnps, false, channel);
   }
   chan.AddSample(background);
   chan.AddSample(signal);
   return chan;
}

void buildBinnedTest_nchannels(int n_channels = 1, int nbins = 10, int nnps = 3, const char *name_rootfile = "")
{
   bool channel_crosstalk = true;
   Measurement meas("meas", "meas");
   meas.SetPOI("SignalStrength");
   meas.SetLumi(1.0);
   meas.SetLumiRelErr(0.10);
   meas.AddConstantParam("Lumi");
   Channel chan;
   for (int channel = 0; channel < n_channels; ++channel) {
      chan = makeChannel(channel, nbins,  nnps, channel_crosstalk);
      meas.AddChannel(chan);
   }
   HistoToWorkspaceFactoryFast hist2workspace(meas);
   RooWorkspace *ws;
   if (n_channels < 2) {
      ws = hist2workspace.MakeSingleChannelModel(meas, chan);
   } else {
      ws = hist2workspace.MakeCombinedModel(meas);
   }
   RooFIter iter = ws->components().fwdIterator();
   RooAbsArg *arg;
   while ((arg = iter.next())) {
      if (arg->IsA() == RooRealSumPdf::Class()) {
         arg->setAttribute("BinnedLikelihood");
         std::cout << "component " << arg->GetName() << " is a binned likelihood" << std::endl;
      }
   }
   ws->SetName("BinnedWorkspace");
   ws->writeToFile(name_rootfile);
}

static void BM_RooFit_BinnedTestMigrad(benchmark::State &state)
{
   gErrorIgnoreLevel = kInfo;
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   int cpu = state.range(0);
   TFile *infile = new TFile("workspace.root");
   if (infile->IsZombie()) {
      buildBinnedTest();
      std::cout << "Workspace for tests was created!" << std::endl;
   }
   infile = TFile::Open("workspace.root");
   RooWorkspace *w = static_cast<RooWorkspace *>(infile->Get("BinnedWorkspace"));
   RooAbsData *data = w->data("obsData");
   ModelConfig *mc = static_cast<ModelConfig *>(w->genobj("ModelConfig"));
   RooAbsPdf *pdf = w->pdf(mc->GetPdf()->GetName());
   RooAbsReal *nll = pdf->createNLL(*data, NumCPU(cpu, 0));
   RooMinimizer m(*nll);
   m.setPrintLevel(-1);
   m.setStrategy(0);
   m.setLogFile("benchmigradlog");
   while (state.KeepRunning()) {
      m.migrad();
   }
   delete data;
   delete infile;
   delete mc;
   delete pdf;
   delete nll;
}

// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Unit(benchmark::kMicrosecond)->Arg(2)->UseRealTime();
// KNL scaling
// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Range(8, 128)->UseRealTime();
// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Range(1, 3)->UseRealTime();

static void BM_RooFit_BinnedTestMigrad_NChannel(benchmark::State &state)
{
   gErrorIgnoreLevel = kInfo;
   int chan = state.range(0);
   int cpu = state.range(1);
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   std::string workspace_file = "worspace" + std::to_string(chan) + std::to_string(cpu) + ".root";
   TFile *infile = new TFile(workspace_file.c_str());
   if (infile->IsZombie()) {
      buildBinnedTest_nchannels(chan, 30, 3, workspace_file.c_str());
      std::cout << "Workspace for tests was created!" << std::endl;
   }
   infile = TFile::Open(workspace_file.c_str());
   RooWorkspace *w = static_cast<RooWorkspace *>(infile->Get("BinnedWorkspace"));
   RooAbsData *data = w->data("obsData");
   ModelConfig *mc = static_cast<ModelConfig *>(w->genobj("ModelConfig"));
   RooAbsPdf *pdf = w->pdf(mc->GetPdf()->GetName());
   RooAbsReal *nll = pdf->createNLL(*data, NumCPU(cpu, 0));
   RooMinimizer m(*nll);
   m.setPrintLevel(-1);
   m.setStrategy(0);
   m.setLogFile("benchmigradnchanellog");
   while (state.KeepRunning()) {
      m.migrad();
   }
   delete data;
   delete infile;
   delete mc;
   delete pdf;
   delete nll;
}


static void ChanArguments(benchmark::internal::Benchmark* b) {
  for (int i = 1; i <8; ++i)
    for (int j = 1; j <= 4; ++j)
      b->Args({i, j});
}

// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Unit(benchmark::kMicrosecond)->Arg(2)->UseRealTime();
// KNL scaling
// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Range(8, 128)->UseRealTime();
// BENCHMARK(BM_RooFit_BinnedTestMigrad_NChannel)->Ranges({{1, 16}, {1, 3}})->UseRealTime();
//BENCHMARK(BM_RooFit_BinnedTestMigrad_NChannel)->Args({1<<10, 1<<10})->UseRealTime();
BENCHMARK(BM_RooFit_BinnedTestMigrad_NChannel)->Apply(ChanArguments)->UseRealTime()->Iterations(12);

static void BM_RooFit_BinnedTestMigrad_NBin(benchmark::State &state)
{
   gErrorIgnoreLevel = kInfo;
   int nbins = state.range(0);
   int cpu = state.range(1);
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   std::string workspace_file = "worspace" + std::to_string(nbins) + std::to_string(cpu) + ".root";
   TFile *infile = new TFile(workspace_file.c_str());
   if (infile->IsZombie()) {
     buildBinnedTest_nchannels(1, nbins, 3, workspace_file.c_str());
      std::cout << "Workspace for tests was created!" << std::endl;
   }
   infile = TFile::Open(workspace_file.c_str());
   RooWorkspace *w = static_cast<RooWorkspace *>(infile->Get("BinnedWorkspace"));
   RooAbsData *data = w->data("obsData");
   ModelConfig *mc = static_cast<ModelConfig *>(w->genobj("ModelConfig"));
   RooAbsPdf *pdf = w->pdf(mc->GetPdf()->GetName());
   RooAbsReal *nll = pdf->createNLL(*data, NumCPU(cpu, 0));
   RooMinimizer m(*nll);
   m.setPrintLevel(-1);
   m.setStrategy(0);
   m.setLogFile("benchmigradnchanellog");
   while (state.KeepRunning()) {
      m.migrad();
   }
   delete data;
   delete infile;
   delete mc;
   delete pdf;
   delete nll;
}

static void BinArguments(benchmark::internal::Benchmark* b) {
  for (int i = 10; i <=100; i += 10)
    for (int j = 1; j <= 4; ++j)
      b->Args({i, j});
}

// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Unit(benchmark::kMicrosecond)->Arg(2)->UseRealTime();
// KNL scaling
// BENCHMARK(BM_RooFit_BinnedTestMigrad)->Range(8, 128)->UseRealTime();
//BENCHMARK(BM_RooFit_BinnedTestMigrad_NBin)->Ranges({{1, 40}, {1, 3}})->UseRealTime();
BENCHMARK(BM_RooFit_BinnedTestMigrad_NBin)->Apply(BinArguments)->UseRealTime()->Iterations(12);

static void BM_RooFit_BinnedTestHesse(benchmark::State &state)
{
   gErrorIgnoreLevel = kInfo;
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   int cpu = state.range(0);
   TFile *infile = new TFile("workspace.root");
   if (infile->IsZombie()) {
      buildBinnedTest();
      std::cout << "Workspace for tests was created!" << std::endl;
   }
   infile->TFile::Open("workspace.root");
   RooWorkspace *w = static_cast<RooWorkspace *>(infile->Get("BinnedWorkspace"));
   RooAbsData *data = w->data("obsData");
   ModelConfig *mc = static_cast<ModelConfig *>(w->genobj("ModelConfig"));
   RooAbsPdf *pdf = w->pdf(mc->GetPdf()->GetName());
   RooAbsReal *nll = pdf->createNLL(*data, NumCPU(cpu, 0));
   RooMinimizer m(*nll);
   m.setPrintLevel(-1);
   m.setStrategy(0);
   m.setLogFile("benchhesselog");
   m.migrad();
   while (state.KeepRunning()) {
      m.hesse();
   }
   delete data;
   delete infile;
   delete mc;
   delete pdf;
   delete nll;
}
// KNL scaling
// BENCHMARK(BM_RooFit_BinnedTestHesse)->Range(8, 128)->UseRealTime();
// BENCHMARK(BM_RooFit_BinnedTestHesse)->Range(1, 3)->UseRealTime();

static void BM_RooFit_BinnedTestMinos(benchmark::State &state)
{
   gErrorIgnoreLevel = kInfo;
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   int cpu = state.range(0);
   TFile *infile = new TFile("workspace.root");
   if (infile->IsZombie()) {
      buildBinnedTest();
      std::cout << "Workspace for tests was created!" << std::endl;
   }
   infile->TFile::Open("workspace.root");
   RooWorkspace *w = static_cast<RooWorkspace *>(infile->Get("BinnedWorkspace"));
   RooAbsData *data = w->data("obsData");
   ModelConfig *mc = static_cast<ModelConfig *>(w->genobj("ModelConfig"));
   RooAbsPdf *pdf = w->pdf(mc->GetPdf()->GetName());
   RooAbsReal *nll = pdf->createNLL(*data, NumCPU(cpu, 0));
   RooMinimizer m(*nll);
   m.setPrintLevel(-1);
   m.setStrategy(0);
   m.setLogFile("benchminoslog");
   m.migrad();
   while (state.KeepRunning()) {
      m.minos();
   }
   delete data;
   delete infile;
   delete mc;
   delete pdf;
   delete nll;
}
// KNL scaling
// BENCHMARK(BM_RooFit_BinnedTestMinos)->Range(8, 128)->UseRealTime();
//BENCHMARK(BM_RooFit_BinnedTestMinos)->Range(1, 3)->UseRealTime();

BENCHMARK_MAIN();
