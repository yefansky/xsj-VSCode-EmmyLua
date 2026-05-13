'use strict';

import * as vscode from 'vscode';
import * as path from "path";
import * as net from "net";
import * as fs from "fs";
import * as process from "process";
import * as Annotator from "./annotator";
import * as notifications from "./notifications";
import { LanguageClient, LanguageClientOptions, ServerOptions, StreamInfo } from "vscode-languageclient/node";
import { LuaLanguageConfiguration } from './languageConfiguration';
import { EmmyDebuggerProvider } from './debugger/EmmyDebuggerProvider';
import { EmmyConfigWatcher, IEmmyConfigUpdate } from './emmyConfigWatcher';
import { EmmyAttachDebuggerProvider } from './debugger/EmmyAttachDebuggerProvider';
import { EmmyLaunchDebuggerProvider } from './debugger/EmmyLaunchDebuggerProvider';
import * as psi from './web/psiViewer';
import { LuaContext } from './luaContext';

// Output channels
let outputChannel: vscode.OutputChannel;
let traceChannel: vscode.OutputChannel;

function log(message: string): void {
    const timestamp = new Date().toLocaleTimeString();
    outputChannel?.appendLine(`[EmmyLua ${timestamp}] ${message}`);
    console.log(`[EmmyLua] ${message}`);
}

function trace(direction: string, method: string, data?: any): void {
    const ts = new Date().toLocaleTimeString();
    const msg = data ? `${direction} ${method}: ${JSON.stringify(data).substring(0, 200)}` : `${direction} ${method}`;
    traceChannel?.appendLine(`[${ts}] ${msg}`);
}

export let luaContext: LuaContext;
let activeEditor: vscode.TextEditor;
let progressBar: vscode.StatusBarItem;
let configWatcher: EmmyConfigWatcher;

export function activate(context: vscode.ExtensionContext) {
    // Create output channels
    outputChannel = vscode.window.createOutputChannel("EmmyLua");
    traceChannel = vscode.window.createOutputChannel("EmmyLua Trace");
    context.subscriptions.push(outputChannel, traceChannel);

    log("=== EmmyLua Extension Activating ===");
    log(`Platform: ${process.platform} ${process.arch}`);
    log(`VSCode Version: ${vscode.version}`);
    log(`Extension Path: ${context.extensionPath}`);
    log(`Debug Mode: ${process.env['EMMY_DEV'] === 'true'}`);

    // Optimize VSCode settings for Lua development
    const editorConfig = vscode.workspace.getConfiguration('editor');
    const currentMaxHintLength = editorConfig.get<number>('inlayHints.maximumLength', 30);
    if (currentMaxHintLength < 1024) {
        editorConfig.update('inlayHints.maximumLength', 1024, vscode.ConfigurationTarget.Global);
        log('Set editor.inlayHints.maximumLength to 1024');
    }

    // Show visible notification to confirm activation
    vscode.window.showInformationMessage(`EmmyLua: Extension activating... (Output > EmmyLua for logs)`);

    // Check workspace
    const workspaceFolders = vscode.workspace.workspaceFolders;
    log(`Workspace: ${workspaceFolders?.map(f => f.uri.fsPath).join(', ') || 'none'}`);

    luaContext = new LuaContext(process.env['EMMY_DEV'] === "true", context);
    progressBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left);

    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(onDidChangeConfiguration));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(onDidChangeTextDocument));
    context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor(onDidChangeActiveTextEditor));
    context.subscriptions.push(vscode.commands.registerCommand("emmy.restartServer", restartServer));
    context.subscriptions.push(vscode.commands.registerCommand("emmy.showReferences", showReferences));
    context.subscriptions.push(vscode.commands.registerCommand("emmy.insertEmmyDebugCode", insertEmmyDebugCode));

    context.subscriptions.push(vscode.languages.setLanguageConfiguration("lua", new LuaLanguageConfiguration()));

    configWatcher = new EmmyConfigWatcher();
    configWatcher.onConfigUpdate(onConfigUpdate);
    context.subscriptions.push(configWatcher);

    startServer();
    registerDebuggers();
    psi.registerCommand(luaContext);

    log("=== Extension Activated ===");
    log("Tip: View logs in Output > EmmyLua");

    return {
        reportAPIDoc: (classDoc: any) => {
            luaContext?.client?.sendRequest("emmy/reportAPI", classDoc);
        }
    }
}

function registerDebuggers() {
    log("Registering debugger providers...");
    const context = luaContext.extensionContext;
    const emmyProvider = new EmmyDebuggerProvider('emmylua_new', context);
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider("emmylua_new", emmyProvider));
    context.subscriptions.push(emmyProvider);
    const emmyAttachProvider = new EmmyAttachDebuggerProvider('emmylua_attach', context);
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('emmylua_attach', emmyAttachProvider));
    context.subscriptions.push(emmyAttachProvider);
    const emmyLaunchProvider = new EmmyLaunchDebuggerProvider('emmylua_launch', context);
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('emmylua_launch', emmyLaunchProvider));
    context.subscriptions.push(emmyLaunchProvider);

    context.subscriptions.push(vscode.languages.registerInlineValuesProvider('lua', {
        provideInlineValues(document: vscode.TextDocument, viewport: vscode.Range, context: vscode.InlineValueContext): vscode.ProviderResult<vscode.InlineValue[]> {
            const allValues: vscode.InlineValue[] = [];
            const regExps = [/(?<=local\s+)[^\s,\<]+/, /(?<=---@param\s+)\S+/];
            for (let l = viewport.start.line; l <= context.stoppedLocation.end.line; l++) {
                const line = document.lineAt(l);
                for (const regExp of regExps) {
                    const match = regExp.exec(line.text);
                    if (match) {
                        const varName = match[0];
                        const varRange = new vscode.Range(l, match.index, l, match.index + varName.length);
                        allValues.push(new vscode.InlineValueVariableLookup(varRange, varName, false));
                        break;
                    }
                }
            }
            return allValues;
        }
    }));
}

function onDidChangeTextDocument(event: vscode.TextDocumentChangeEvent) {
    if (activeEditor && activeEditor.document === event.document
        && activeEditor.document.languageId === luaContext.LANGUAGE_ID
        && luaContext.client != undefined) {
        trace("NOTIFY", "textDocument/didChange", { uri: event.document.uri.toString() });
        Annotator.requestAnnotators(activeEditor, luaContext.client);
    }
}

function onDidChangeActiveTextEditor(editor: vscode.TextEditor | undefined) {
    if (editor && editor.document.languageId === luaContext.LANGUAGE_ID && luaContext.client != undefined) {
        activeEditor = editor as vscode.TextEditor;
        trace("NOTIFY", "activeEditor changed", { uri: editor.document.uri.toString() });
        Annotator.requestAnnotators(activeEditor, luaContext.client);
    }
}

export function deactivate() {
    log("=== Extension Deactivating ===");
    stopServer();
}

function onDidChangeConfiguration(event: vscode.ConfigurationChangeEvent) {
    log("Configuration changed");
    if (luaContext.client !== undefined) {
        Annotator.onDidChangeConfiguration(luaContext.client);
    }
}

async function startServer() {
    log("[Server] Starting...");
    doStartServer().then(() => {
        log("[Server] Started successfully");
        onDidChangeActiveTextEditor(vscode.window.activeTextEditor);
    }).catch(reason => {
        log(`[Server] ERROR: ${reason}`);
        vscode.window.showErrorMessage(`Failed to start "EmmyLua" language server!\n${reason}`, "Try again")
            .then(startServer);
    });
}

function getServerBinaryName(): string {
    return process.platform === 'win32' ? 'emmylua-ls.exe' : 'emmylua-ls';
}

async function doStartServer() {
    const configFiles = await configWatcher.watch();
    const context = luaContext.extensionContext;

    const serverBin = getServerBinaryName();
    const serverPath = path.resolve(context.extensionPath, "server", serverBin);
    const serverExists = fs.existsSync(serverPath);

    log(`[Server] Binary: ${serverPath}`);
    log(`[Server] Exists: ${serverExists}`);

    if (!serverExists) {
        throw new Error(`Server binary not found: ${serverPath}`);
    }

    const stdFolder = path.resolve(context.extensionPath, "res/std");
    log(`[Server] stdFolder: ${stdFolder}`);
    log(`[Server] Config files: ${configFiles.length}`);

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: luaContext.LANGUAGE_ID }],
        synchronize: {
            configurationSection: ["emmylua", "files.associations"],
            fileEvents: [vscode.workspace.createFileSystemWatcher("**/*.lua")]
        },
        initializationOptions: {
            stdFolder: vscode.Uri.file(stdFolder).toString(),
            apiFolders: [],
            client: 'vsc',
            configFiles: configFiles
        },
        outputChannel: outputChannel,
    };

    let serverOptions: ServerOptions;
    if (luaContext.debugMode) {
        log("[Server] Mode: TCP (port 5007)");
        serverOptions = () => {
            log("[Server] Connecting to localhost:5007...");
            let socket = net.connect({ port: 5007 });
            let result: StreamInfo = {
                writer: socket,
                reader: socket as NodeJS.ReadableStream
            };
            socket.on("connect", () => log("[Server] TCP connected"));
            socket.on("close", () => log("[Server] TCP disconnected"));
            socket.on("error", (err) => log(`[Server] TCP error: ${err.message}`));
            return Promise.resolve(result);
        };
    } else {
        log("[Server] Mode: stdio");
        log(`[Server] Command: ${serverPath} --stdio`);
        serverOptions = {
            command: serverPath,
            args: ["--stdio"]
        };
    }

    log("[Client] Creating language client...");
    luaContext.client = new LanguageClient(
        luaContext.LANGUAGE_ID,
        "EmmyLua",
        serverOptions,
        clientOptions
    );

    // Capture server log messages
    luaContext.client.onNotification("window/logMessage", (params) => {
        log(`[Server] ${params.message}`);
    });

    // Trace LSP lifecycle
    luaContext.client.onDidChangeState((event) => {
        const stateNames = ['Stopped', 'Starting', 'Running'];
        log(`[Client] State: ${stateNames[event.oldState]} -> ${stateNames[event.newState]}`);
    });

    log("[Client] Starting...");
    await luaContext.client.start();
    log("[Client] Connected to server");

    // Listen for progress reports
    luaContext.client.onNotification("emmy/progressReport", (d: notifications.IProgressReport) => {
        progressBar.show();
        progressBar.text = d.text;
        trace("NOTIFY", "emmy/progressReport", d);
        if (d.percent >= 1) {
            setTimeout(() => progressBar.hide(), 3000);
        }
    });

    // Listen for workspace indexing completion
    luaContext.client.onNotification("emmy/indexingDone", () => {
        log("Workspace indexing complete");
        vscode.window.showInformationMessage("EmmyLua: Workspace indexed. Re-request references for updated results.");
    });

    // Test the server with a simple request
    log("[Client] Verifying server response...");
    try {
        await luaContext.client.sendRequest("textDocument/completion", {
            textDocument: { uri: "file:///test" },
            position: { line: 0, character: 0 }
        });
        log("[Client] Server responding to requests ✓");
    } catch (e) {
        log(`[Client] Verification request completed`);
    }
}

function restartServer() {
    log("[Server] Restarting...");
    const client = luaContext.client;
    if (!client) {
        startServer();
    } else {
        client.stop().then(() => {
            log("[Server] Stopped");
            startServer();
        });
    }
}

function showReferences(uri: string, pos: vscode.Position) {
    log(`[Command] showReferences: ${uri}:${pos.line}:${pos.character}`);
    const u = vscode.Uri.parse(uri);
    const p = new vscode.Position(pos.line, pos.character);
    vscode.commands.executeCommand("vscode.executeReferenceProvider", u, p).then(locations => {
        vscode.commands.executeCommand("editor.action.showReferences", u, p, locations);
    });
}

function stopServer() {
    const client = luaContext.client;
    if (client) {
        log("[Server] Stopping...");
        client.stop();
    }
}

function onConfigUpdate(e: IEmmyConfigUpdate) {
    log(`[Config] Update: ${e.type} ${e.source.uri}`);
    const client = luaContext.client;
    if (client) {
        client.sendRequest('emmy/updateConfig', e);
    }
}

async function insertEmmyDebugCode() {
    const context = luaContext.extensionContext;
    const activeEditor = vscode.window.activeTextEditor;
    if (!activeEditor) return;
    const document = activeEditor.document;
    if (document.languageId !== 'lua') return;

    let dllPath = '';
    const isWindows = process.platform === 'win32';
    const isMac = process.platform === 'darwin';
    const isLinux = process.platform === 'linux';

    if (isWindows) {
        const arch = await vscode.window.showQuickPick(['x64', 'x86']);
        if (!arch) return;
        dllPath = path.join(context.extensionPath, `debugger/emmy/windows/${arch}/?.dll`);
    } else if (isMac) {
        const arch = await vscode.window.showQuickPick(['x64', 'arm64']);
        if (!arch) return;
        dllPath = path.join(context.extensionPath, `debugger/emmy/mac/${arch}/emmy_core.dylib`);
    } else if (isLinux) {
        dllPath = path.join(context.extensionPath, `debugger/emmy/linux/emmy_core.so`);
    }

    const host = '127.0.0.1';
    const port = 9966;
    const ins = new vscode.SnippetString();
    ins.appendText(`package.cpath = package.cpath .. ";${dllPath.replace(/\\/g, '/')}"\n`);
    ins.appendText(`local dbg = require("emmy_core")\n`);
    ins.appendText(`dbg.tcpListen("${host}", ${port})`);
    activeEditor.insertSnippet(ins);
}
