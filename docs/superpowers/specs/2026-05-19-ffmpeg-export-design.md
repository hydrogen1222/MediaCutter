# FFmpeg Export Design

## Goal
Implement the ability to export all marked segments from the input video into a single output file using FFmpeg.

## Architecture
- **FFmpegRunner**: A worker class that manages the multi-step FFmpeg process.
    - Step 1: Extract each segment into a temporary TS file.
    - Step 2: Create a concat file listing these segments.
    - Step 3: Use FFmpeg's concat demuxer to merge them into the final output.
    - Step 4: Cleanup temporary files.
- **MainWindow**: Triggers the export, gathers output path, and displays progress using `QProgressDialog`.
- **ClipModel**: Provides the list of segments to be exported.

## Components

### ClipModel
- Method: `const std::vector<Segment>& segments() const`
    - Returns the internal list of segments.

### FFmpegRunner
- Inherits from `QObject`.
- Method: `void cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output)`
    - This will likely be asynchronous, using `QProcess`.
- Signals:
    - `progress(int current, int total, QString status)`
    - `finished(bool success, QString message)`
    - `error(QString message)`

### MainWindow
- Slot: `void exportAll()`
    - Prompts for output file using `QFileDialog::getSaveFileName`.
    - Creates `FFmpegRunner`.
    - Creates `QProgressDialog`.
    - Connects signals to update dialog and handle completion.

## Error Handling
- FFmpeg process failures (monitored via `QProcess` signals).
- File system errors (writing temp files).
- User cancellation (aborting `QProgressDialog` should stop the process).

## Verification
- Unit test for `ClipModel::segments()`.
- Manual verification of the export process with a sample video.
