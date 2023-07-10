REM Putting output of command `hostname` into variable HOST. Based on https://stackoverflow.com/a/48404829.
FOR /F "tokens=*" %%g IN ('hostname') do (SET HOST=%%g)
SET DIRNAME=results-win-%HOST%

rmdir /S /Q %DIRNAME%
mkdir %DIRNAME%

wmic cpu get caption, deviceid, name, numberofcores, maxclockspeed, L2CacheSize, L3CacheSize, NumberOfCores, NumberOfLogicalProcessors, SocketDesignation /format:list > %DIRNAME%\cpuinfo.txt
echo "win" > $DIRNAME/os.txt

cd benchmark

26-bench-rect-drawing --gl-debug-log --capture-last-frames --write-gl-info --only-one-frame > debug-approaches.csv 2> debug-frames.csv
26-bench-rect-drawing                           >  bench-approaches.csv 2>  bench-frames.csv
26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
26-bench-rect-drawing --disable-timer-queries                           >  bench-untimed-approaches.csv 2>  bench-untimed-frames.csv
26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
move *.csv ..\%DIRNAME%
move *.txt ..\%DIRNAME%
move *.ppm ..\%DIRNAME%
