const fs = require('fs');
const download = require('download');
const decompress = require('decompress')
const fc = require('filecopy');
const config = require('./config').default;
const path = require('path');

async function downloadTo(url, path) {
    return new Promise((r, e) => {
        const d = download(url);
        d.then(r).catch(err => e(err));
        d.pipe(fs.createWriteStream(path));
    });
}

async function downloadDepends() {
    await Promise.all([
        downloadTo(`${config.emmyDebuggerUrl}/${config.emmyDebuggerVersion}/linux-x64.zip`, 'temp/linux-x64.zip'),
        downloadTo(`${config.emmyDebuggerUrl}/${config.emmyDebuggerVersion}/darwin-arm64.zip`, 'temp/darwin-arm64.zip'),
        downloadTo(`${config.emmyDebuggerUrl}/${config.emmyDebuggerVersion}/darwin-x64.zip`, 'temp/darwin-x64.zip'),
        downloadTo(`${config.emmyDebuggerUrl}/${config.emmyDebuggerVersion}/win32-x86.zip`, 'temp/win32-x86.zip'),
        downloadTo(`${config.emmyDebuggerUrl}/${config.emmyDebuggerVersion}/win32-x64.zip`, 'temp/win32-x64.zip'),
    ]);
}

// Copy the C++ language server binary from the build output
async function copyCppServer() {
    const buildDir = path.resolve(__dirname, '..', 'emmylua-ls', 'build', 'Release');
    const serverDir = path.resolve(__dirname, '..', 'server');

    // Create server directory if it doesn't exist
    if (!fs.existsSync(serverDir)) {
        fs.mkdirSync(serverDir, { recursive: true });
    }

    // Try to find the built binary
    const isWin = process.platform === 'win32';
    const binaryName = isWin ? 'emmylua-ls.exe' : 'emmylua-ls';
    const srcPath = path.join(buildDir, binaryName);
    const destPath = path.join(serverDir, binaryName);

    if (fs.existsSync(srcPath)) {
        console.log(`Copying ${binaryName} from build output...`);
        await fc(srcPath, destPath, { mkdirp: true });
        console.log(`Copied to ${destPath}`);
    } else {
        console.log(`Warning: ${binaryName} not found at ${srcPath}`);
        console.log('Please build the C++ server first:');
        console.log('  cd emmylua-ls && mkdir -p build && cd build && cmake .. && cmake --build . --config Release');
    }
}

async function build() {
    if (!fs.existsSync('temp')) {
        fs.mkdirSync('temp')
    }

    // Download debugger binaries
    await downloadDepends();

    // linux
    await decompress('temp/linux-x64.zip', 'debugger/emmy/linux/');
    // mac
    await decompress('temp/darwin-x64.zip', 'debugger/emmy/mac/x64/');
    await decompress('temp/darwin-arm64.zip', 'debugger/emmy/mac/arm64/');
    // win
    await decompress('temp/win32-x86.zip', 'debugger/emmy/windows/x86/');
    await decompress('temp/win32-x64.zip', 'debugger/emmy/windows/x64/');

    // Copy C++ language server
    await copyCppServer();
}

build().catch(console.error);
