DIRNAME=results-lin-$(hostname)

rm -rf $DIRNAME
mkdir -p $DIRNAME

cat /proc/cpuinfo | grep "^model name" | tail -n 1 | cut --delimiter=: --fields=2 | cut --characters=2- > $DIRNAME/cpuname.txt
cat /proc/cpuinfo > $DIRNAME/cpuinfo.txt
echo "lin" > $DIRNAME/os.txt

cd benchmark

./26-bench-rect-drawing --gl-debug-log --capture-last-frames --write-gl-info --only-one-frame > debug-approaches.csv 2> debug-frames.csv
./26-bench-rect-drawing                           >  bench-approaches.csv 2>  bench-frames.csv
./26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
./26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
./26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
./26-bench-rect-drawing --dont-output-csv-headers >> bench-approaches.csv 2>> bench-frames.csv
./26-bench-rect-drawing --disable-timer-queries                           >  bench-untimed-approaches.csv 2>  bench-untimed-frames.csv
./26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
./26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
./26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
./26-bench-rect-drawing --disable-timer-queries --dont-output-csv-headers >> bench-untimed-approaches.csv 2>> bench-untimed-frames.csv
mv *.csv ../$DIRNAME
mv *.txt ../$DIRNAME
mv *.ppm ../$DIRNAME
