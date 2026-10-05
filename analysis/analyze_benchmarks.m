%% analyze_benchmarks.m
% RTGP dungeon benchmark analysis (Andrea).
%
% Loads every per-frame CSV written by the benchmark harness, groups the files by
% configuration (pooling repeated runs "_run2/_run3/..."), drops the warm-up, and produces:
%   * a summary table (mean FPS, median / 95th-pct / 1%-low frame time, draw calls,
%     triangles, % culled) written to summary_table.csv and printed to the console;
%   * the figures for the report (culling over time + distribution, bottleneck scatter,
%     particle instancing, structural instancing, shadow-light sweep, fog sweep, SSAO cost).
%
% The honest per-frame metric is `frame_ms` (from deltaTime); `fps_smoothed` is ImGui's
% smoothed value and is used only for reference, never for the stats. See analysis_notes.
%
% Requires MATLAB R2016b+ (containers.Map, string functions, local functions in scripts).
% No toolbox needed: percentiles are computed by a local pctl() function.

clear; close all; clc;

%% ---- configuration
% Where the CSVs live. Leave benchDir = '' to auto-detect: the script assumes it sits in
% <repo>/analysis/ and the CSVs are in <repo>/out/build/x64-Release/benchmarks/. If that
% folder is not found it falls back to the script's own directory. Override if needed.
benchDir = '';
if isempty(benchDir)
    here = fileparts(mfilename('fullpath'));
    cand = fullfile(here, '..', 'out', 'build', 'x64-Release', 'benchmarks');
    if isfolder(cand), benchDir = cand; else, benchDir = here; end
end

seed     = 12345;    % which dungeon's runs to analyse (matches the CSV file names)
warmupMs = 500;      % drop the first half second of every run (caches / spin-up)

figDir = fullfile(benchDir, 'figures', sprintf('seed%d', seed));   % per-seed so runs don't overwrite
if ~isfolder(figDir), mkdir(figDir); end

fprintf('Benchmark folder: %s\n', benchDir);
fprintf('Seed            : %d\n\n', seed);

%% ---- discover + load all CSVs, grouped by config, pooling repeated runs
files = dir(fullfile(benchDir, sprintf('benchmark_seed%d_*.csv', seed)));
assert(~isempty(files), 'No CSVs found for seed %d in %s', seed, benchDir);

data = containers.Map('KeyType', 'char', 'ValueType', 'any');   % config name -> struct

for k = 1:numel(files)
    fname = files(k).name;
    [cfg, ~] = parseName(fname, seed);

    T = loadCsv(fullfile(benchDir, fname), warmupMs);
    if isempty(T), continue; end

    if ~isKey(data, cfg)
        s = struct();
        s.pooled    = T;                        % all frames of all runs (for distributions)
        s.first     = T;                        % first run only (for the time-series plots)
        s.runMeanMs = mean(T.frame_ms);         % one mean per run (for run-to-run std / error bars)
        s.nRuns     = 1;
        data(cfg)   = s;
    else
        s = data(cfg);
        s.pooled            = [s.pooled; T];
        s.runMeanMs(end+1)  = mean(T.frame_ms);
        s.nRuns             = s.nRuns + 1;
        data(cfg)           = s;
    end
end

cfgs = keys(data);
fprintf('Loaded %d files -> %d configurations.\n', numel(files), numel(cfgs));
for i = 1:numel(cfgs)
    fprintf('   %-22s  %d run(s)\n', cfgs{i}, data(cfgs{i}).nRuns);
end
fprintf('\n');

%% ---- summary table
% Preferred display order; any config not listed is appended at the end.
order = {'cullON_ssaoON','cullOFF_ssaoON','cullON_ssaoOFF','cullOFF_ssaoOFF', ...
         'structInstOFF','structInstON','instancedOFF','instancedON', ...
         'shadowSpot1','shadowSpot2','shadowSpot4','shadowSpot6','shadowSpot8', ...
         'fogSteps4','fogSteps8','fogSteps12','fogSteps16','fogSteps24', ...
         'fogSteps32','fogSteps48','fogSteps64'};
ordered = [order(ismember(order, cfgs)), setdiff(cfgs, order)];

Config=strings(0); nRuns=[]; N=[]; avgFPS=[]; medMs=[]; p95Ms=[]; low1FPS=[];
meanMs=[]; stdMs=[]; meanDraw=[]; meanTri=[]; pctCull=[];

for i = 1:numel(ordered)
    s  = data(ordered{i});
    fm = s.pooled.frame_ms;
    Config(end+1,1) = ordered{i};
    nRuns(end+1,1)  = s.nRuns;
    N(end+1,1)      = numel(fm);
    avgFPS(end+1,1) = 1000 / mean(fm);
    medMs(end+1,1)  = median(fm);
    p95Ms(end+1,1)  = pctl(fm, 95);
    thr             = pctl(fm, 99);                 % slowest 1% of frames
    low1FPS(end+1,1)= 1000 / mean(fm(fm >= thr));
    meanMs(end+1,1) = mean(fm);
    stdMs(end+1,1)  = std(s.runMeanMs);             % run-to-run spread (0 with a single run)
    meanDraw(end+1,1)= mean(s.pooled.draw_calls);
    meanTri(end+1,1) = mean(s.pooled.triangles);
    pctCull(end+1,1) = mean(s.pooled.objects_culled ./ s.pooled.objects_total) * 100;
end

summary = table(Config, nRuns, N, avgFPS, medMs, p95Ms, low1FPS, meanMs, stdMs, ...
                meanDraw, meanTri, pctCull);
disp(summary);
summaryFile = fullfile(benchDir, sprintf('summary_table_seed%d.csv', seed));   % per-seed name
writetable(summary, summaryFile);
fprintf('Wrote %s\n\n', summaryFile);

%% ---- headline findings (printed)
% Culling effect at the default pipeline (SSAO ON).
if isKey(data,'cullON_ssaoON') && isKey(data,'cullOFF_ssaoON')
    on = data('cullON_ssaoON'); off = data('cullOFF_ssaoON');
    dDraw = (mean(off.pooled.draw_calls) - mean(on.pooled.draw_calls)) / mean(off.pooled.draw_calls) * 100;
    dTri  = (mean(off.pooled.triangles)  - mean(on.pooled.triangles))  / mean(off.pooled.triangles)  * 100;
    dMs   = mean(off.pooled.frame_ms) - mean(on.pooled.frame_ms);
    fpsOn = 1000/mean(on.pooled.frame_ms); fpsOff = 1000/mean(off.pooled.frame_ms);
    fprintf('=== Culling (SSAO ON) ===\n');
    fprintf('  draw calls   : %.0f -> %.0f  (-%.1f%%)\n', mean(off.pooled.draw_calls), mean(on.pooled.draw_calls), dDraw);
    fprintf('  triangles    : %.0f -> %.0f  (-%.1f%%)\n', mean(off.pooled.triangles),  mean(on.pooled.triangles),  dTri);
    fprintf('  frame time   : %.2f -> %.2f ms  (-%.2f ms)\n', mean(off.pooled.frame_ms), mean(on.pooled.frame_ms), dMs);
    fprintf('  fps          : %.1f -> %.1f  (+%.1f%%)\n', fpsOff, fpsOn, (fpsOn/fpsOff-1)*100);
    fprintf('  culled along path: %.1f%%\n\n', mean(on.pooled.objects_culled ./ on.pooled.objects_total)*100);
end
% SSAO cost (ON minus OFF), at each culling level.
printDelta(data, 'cullON_ssaoOFF',  'cullON_ssaoON',  'SSAO cost @ cullON');
printDelta(data, 'cullOFF_ssaoOFF', 'cullOFF_ssaoON', 'SSAO cost @ cullOFF');
% Instancing.
printDelta(data, 'instancedON',  'instancedOFF',  'Particle naive vs instanced');
printDelta(data, 'structInstON', 'structInstOFF', 'Structural instanced vs per-object');
if isKey(data,'structInstON') && isKey(data,'structInstOFF')
    fprintf('  (structural draw calls: %.0f -> %.0f, triangles %.0f -> %.0f)\n\n', ...
        mean(data('structInstOFF').pooled.draw_calls), mean(data('structInstON').pooled.draw_calls), ...
        mean(data('structInstOFF').pooled.triangles),  mean(data('structInstON').pooled.triangles));
end

%% ==== FIGURES =======================================================================
mainOn  = 'cullON_ssaoON';
mainOff = 'cullOFF_ssaoON';

%% Fig 1: frame time over time, culling ON vs OFF (first run of each)
if isKey(data,mainOn) && isKey(data,mainOff)
    f = figure('Name','frame time over time'); hold on;
    a = data(mainOn).first; b = data(mainOff).first;
    plot(b.tMs/1000, b.frame_ms, 'LineWidth', 0.8);
    plot(a.tMs/1000, a.frame_ms, 'LineWidth', 0.8);
    legend('culling OFF','culling ON','Location','best');
    xlabel('t (s)'); ylabel('frame time (ms)'); grid on;
    title('Frame time along the path'); saveFig(f, figDir, 'fig1_frametime_over_time');
end

%% Fig 2: draw calls over time, culling ON vs OFF
if isKey(data,mainOn) && isKey(data,mainOff)
    f = figure('Name','draw calls over time'); hold on;
    a = data(mainOn).first; b = data(mainOff).first;
    plot(b.tMs/1000, b.draw_calls, 'LineWidth', 0.8);
    plot(a.tMs/1000, a.draw_calls, 'LineWidth', 0.8);
    legend('culling OFF','culling ON','Location','best');
    xlabel('t (s)'); ylabel('draw calls'); grid on;
    title('Draw calls along the path (culling is view-direction dependent)');
    saveFig(f, figDir, 'fig2_drawcalls_over_time');
end

%% Fig 3: frame time distribution (CDF + histogram), culling ON vs OFF
if isKey(data,mainOn) && isKey(data,mainOff)
    f = figure('Name','frame time distribution','Position',[100 100 900 380]);
    subplot(1,2,1); hold on;
    histogram(data(mainOff).pooled.frame_ms, 50);
    histogram(data(mainOn).pooled.frame_ms, 50);
    legend('OFF','ON','Location','best'); xlabel('frame time (ms)'); ylabel('# frames');
    title('Histogram'); grid on;
    subplot(1,2,2); hold on;
    plotCDF(data(mainOff).pooled.frame_ms, 'LineWidth', 1.3);
    plotCDF(data(mainOn).pooled.frame_ms,  'LineWidth', 1.3);
    legend('culling OFF','culling ON','Location','best');
    xlabel('frame time (ms)'); ylabel('cumulative fraction'); title('CDF'); grid on;
    saveFig(f, figDir, 'fig3_frametime_distribution');
end

%% Fig 4: bottleneck diagnostic: does frame time track draw calls?
% Pool the culling ON+OFF frames: if frame_ms does NOT rise with draw_calls, the scene is
% not submission-bound (the interesting negative result).
if isKey(data,mainOn) && isKey(data,mainOff)
    dc = [data(mainOn).pooled.draw_calls; data(mainOff).pooled.draw_calls];
    fm = [data(mainOn).pooled.frame_ms;  data(mainOff).pooled.frame_ms];
    R  = corrcoef(dc, fm); r = R(1,2);   % corrcoef is base MATLAB (corr needs the Stats Toolbox)
    f = figure('Name','bottleneck scatter');
    scatter(dc, fm, 6, 'filled', 'MarkerFaceAlpha', 0.15);
    xlabel('draw calls'); ylabel('frame time (ms)'); grid on;
    title(sprintf('Frame time vs draw calls  (Pearson r = %.2f)', r));
    saveFig(f, figDir, 'fig4_bottleneck_drawcalls');
end

%% Fig 5: particle instancing: naive vs instanced
if isKey(data,'instancedON') && isKey(data,'instancedOFF')
    mOff = mean(data('instancedOFF').pooled.frame_ms); eOff = std(data('instancedOFF').runMeanMs);
    mOn  = mean(data('instancedON').pooled.frame_ms);  eOn  = std(data('instancedON').runMeanMs);
    f = figure('Name','particle instancing');
    b = bar([mOff mOn], 0.5); hold on;
    errorbar([1 2], [mOff mOn], [eOff eOn], 'k', 'LineStyle','none', 'LineWidth', 1);
    set(gca,'XTickLabel',{'naive (1 call/particle)','instanced (1 call)'});
    ylabel('frame time (ms)'); grid on;
    title(sprintf('Particle instancing  (\\Delta = %.2f ms)', mOff - mOn));
    saveFig(f, figDir, 'fig5_particle_instancing');
end

%% Fig 6: structural instancing: draw calls collapse, frame time flat
if isKey(data,'structInstON') && isKey(data,'structInstOFF')
    dOff = mean(data('structInstOFF').pooled.draw_calls); dOn = mean(data('structInstON').pooled.draw_calls);
    mOff = mean(data('structInstOFF').pooled.frame_ms);   mOn = mean(data('structInstON').pooled.frame_ms);
    eOff = std(data('structInstOFF').runMeanMs);          eOn = std(data('structInstON').runMeanMs);
    f = figure('Name','structural instancing','Position',[100 100 800 360]);
    subplot(1,2,1); bar([dOff dOn], 0.5);
    set(gca,'XTickLabel',{'per-object','instanced'}); ylabel('draw calls');
    title('Draw calls'); grid on;
    subplot(1,2,2); bar([mOff mOn], 0.5); hold on;
    errorbar([1 2], [mOff mOn], [eOff eOn], 'k', 'LineStyle','none', 'LineWidth', 1);
    set(gca,'XTickLabel',{'per-object','instanced'}); ylabel('frame time (ms)');
    title('Frame time'); grid on;
    saveFig(f, figDir, 'fig6_structural_instancing');
end

%% Fig 7: shadow-light sweep: frame time vs number of SPOT casters
[xs, ms, es] = sweepSeries(data, 'shadowSpot');
if ~isempty(xs)
    f = figure('Name','shadow-light sweep');
    errorbar(xs, ms, es, '-o', 'LineWidth', 1.3);
    xlabel('SPOT shadow casters'); ylabel('frame time (ms)'); grid on;
    title('Frame time vs number of shadow casters');
    saveFig(f, figDir, 'fig7_shadow_sweep');
end

%% Fig 8: fog sweep: frame time vs ray-march steps
[xf, mf, ef] = sweepSeries(data, 'fogSteps');
if ~isempty(xf)
    f = figure('Name','fog sweep');
    errorbar(xf, mf, ef, '-o', 'LineWidth', 1.3);
    xlabel('fog ray-march steps'); ylabel('frame time (ms)'); grid on;
    title('Fog quality vs frame time');
    saveFig(f, figDir, 'fig8_fog_sweep');
end

%% Fig 9: SSAO cost (2x2): frame time by culling x SSAO
q = {'cullON_ssaoOFF','cullON_ssaoON','cullOFF_ssaoOFF','cullOFF_ssaoON'};
if all(cellfun(@(c) isKey(data,c), q))
    M = cellfun(@(c) mean(data(c).pooled.frame_ms), q);
    E = cellfun(@(c) std(data(c).runMeanMs), q);
    f = figure('Name','SSAO cost (2x2)');
    bar(reshape(M,2,2)', 0.7); hold on;   % rows = SSAO OFF/ON, cols = cullON/cullOFF
    set(gca,'XTickLabel',{'culling ON','culling OFF'});
    legend('SSAO OFF','SSAO ON','Location','best'); ylabel('frame time (ms)'); grid on;
    title('Culling x SSAO (2x2)'); saveFig(f, figDir, 'fig9_ssao_2x2');
end

fprintf('\nDone. Figures saved under: %s\n', figDir);

%% ==== local functions ==============================================================
function T = loadCsv(fn, warmupMs)
    % Read one benchmark CSV (skipping the '#' metadata line) and drop the warm-up frames.
    % If readtable did not pick up the column header, name the columns by position instead.
    T = readtable(fn, 'CommentStyle', '#', 'Delimiter', ',');
    want = {'frame','tMs','frame_ms','fps_smoothed','draw_calls','objects_total', ...
            'objects_culled','triangles','lights_active','shadow_lights','shadow_passes', ...
            'fog_steps','particles','particle_draw_calls','cam_x','cam_y','cam_z','yaw','pitch'};
    if ~all(ismember({'tMs','frame_ms'}, T.Properties.VariableNames)) && size(T,2) >= numel(want)
        T.Properties.VariableNames(1:numel(want)) = want;
    end
    T = T(T.tMs > warmupMs, :);
end

function [cfg, runNo] = parseName(fname, seed)
    % "benchmark_seed12345_cullON_ssaoON_run2.csv" -> cfg="cullON_ssaoON", runNo=2
    % "benchmark_seed12345_cullON_ssaoON.csv"      -> cfg="cullON_ssaoON", runNo=1
    stem = erase(fname, '.csv');
    stem = erase(stem, sprintf('benchmark_seed%d_', seed));
    runNo = 1;
    m = regexp(stem, '_run(\d+)$', 'tokens', 'once');   % {} if no "_runN" suffix (the first run)
    if ~isempty(m)
        runNo = str2double(m{1});
        cfg   = regexprep(stem, '_run\d+$', '');
    else
        cfg = stem;
    end
end

function p = pctl(x, q)
    % Percentile without the Statistics Toolbox (linear interpolation, ~MATLAB prctile).
    x = sort(x(:)); n = numel(x);
    if n == 0, p = NaN; return; end
    if n == 1, p = x(1); return; end
    pos = (q/100) * n + 0.5;            % MATLAB's midpoint convention
    pos = min(max(pos, 1), n);
    lo = floor(pos); hi = ceil(pos);
    if lo == hi, p = x(lo); else, p = x(lo) + (pos - lo) * (x(hi) - x(lo)); end
end

function plotCDF(x, varargin)
    x = sort(x(:)); y = (1:numel(x))' / numel(x);
    plot(x, y, varargin{:});
end

function [x, m, e] = sweepSeries(data, prefix)
    % Collect a numeric sweep (e.g. shadowSpot1..8, fogSteps4..64): x = trailing number,
    % m = pooled mean frame_ms, e = run-to-run std of the per-run means (error bar).
    ks = keys(data); x = []; m = []; e = [];
    for i = 1:numel(ks)
        if startsWith(ks{i}, prefix)
            num = str2double(regexp(ks{i}, '\d+$', 'match', 'once'));
            s = data(ks{i});
            x(end+1) = num; %#ok<AGROW>
            m(end+1) = mean(s.pooled.frame_ms); %#ok<AGROW>
            e(end+1) = std(s.runMeanMs); %#ok<AGROW>
        end
    end
    [x, idx] = sort(x); m = m(idx); e = e(idx);
end

function printDelta(data, cfgA, cfgB, label)
    % Print mean frame_ms of B minus A (B is the "more expensive" one by convention).
    if isKey(data, cfgA) && isKey(data, cfgB)
        mA = mean(data(cfgA).pooled.frame_ms); mB = mean(data(cfgB).pooled.frame_ms);
        sA = std(data(cfgA).runMeanMs);         sB = std(data(cfgB).runMeanMs);
        fprintf('=== %s ===\n', label);
        fprintf('  %s: %.2f ms (+/- %.2f) | %s: %.2f ms (+/- %.2f) | delta = %.2f ms\n\n', ...
            cfgA, mA, sA, cfgB, mB, sB, mB - mA);
    end
end

function saveFig(fig, figDir, name)
    fn = fullfile(figDir, [name '.png']);
    try
        exportgraphics(fig, fn, 'Resolution', 150);
    catch
        saveas(fig, fn);
    end
    fprintf('  saved %s\n', fn);
end
