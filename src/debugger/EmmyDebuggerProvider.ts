'use strict';

import * as vscode from 'vscode';
import { EmmyDebugConfiguration } from './types';
import { luaContext } from '../extension';
import { DebuggerProvider } from './DebuggerProvider';

const RECENT_PORTS_KEY = 'emmylua.recentPorts';
const MAX_RECENT_PORTS = 5;

interface PortPickItem extends vscode.QuickPickItem {
    port?: number;
}

function parsePort(value: string): number | undefined {
    const port = parseInt(value);
    if (!isNaN(port) && port >= 1 && port <= 65535) {
        return port;
    }
    return undefined;
}

export class EmmyDebuggerProvider extends DebuggerProvider {
    private showWaitConnectionToken = new vscode.CancellationTokenSource();

    private getRecentPorts(): number[] {
        return this.context.globalState.get<number[]>(RECENT_PORTS_KEY, []);
    }

    private saveRecentPort(port: number) {
        let ports = this.getRecentPorts();
        ports = ports.filter(p => p !== port);
        ports.unshift(port);
        if (ports.length > MAX_RECENT_PORTS) {
            ports = ports.slice(0, MAX_RECENT_PORTS);
        }
        this.context.globalState.update(RECENT_PORTS_KEY, ports);
    }

    private pickPort(currentPort: number): Promise<number | undefined> {
        const recentPorts = this.getRecentPorts();

        const buildItems = (inputValue: string): PortPickItem[] => {
            const items: PortPickItem[] = [];

            const typedPort = parsePort(inputValue);
            if (typedPort !== undefined && !recentPorts.includes(typedPort)) {
                items.push({
                    label: `$(debug-alt)  Port ${typedPort}`,
                    description: 'use this port',
                    port: typedPort
                });
            }

            for (let i = 0; i < recentPorts.length; i++) {
                items.push({
                    label: `$(history)  Port ${recentPorts[i]}`,
                    description: i === 0 ? 'last used' : '',
                    port: recentPorts[i]
                });
            }

            return items;
        };

        return new Promise<number | undefined>(resolve => {
            const qp = vscode.window.createQuickPick<PortPickItem>();
            qp.title = 'EmmyLua Debugger';
            qp.placeholder = 'Select or type a debug port (must match dbg.tcpListen in Lua)';
            qp.items = buildItems('');
            qp.matchOnDescription = false;
            qp.matchOnDetail = false;

            let resolved = false;
            const done = (port: number | undefined) => {
                if (!resolved) {
                    resolved = true;
                    resolve(port);
                }
            };

            qp.onDidChangeValue(value => { qp.items = buildItems(value); });

            qp.onDidAccept(() => {
                qp.hide();
                const port = parsePort(qp.value) ?? qp.selectedItems[0]?.port;
                done(port);
            });

            qp.onDidHide(() => {
                qp.dispose();
                done(undefined);
            });

            qp.show();
        });
    }

    async resolveDebugConfiguration(
        folder: vscode.WorkspaceFolder | undefined,
        debugConfiguration: EmmyDebugConfiguration,
        token?: vscode.CancellationToken
    ): Promise<vscode.DebugConfiguration> {
        debugConfiguration.extensionPath = luaContext.extensionContext.extensionPath;
        debugConfiguration.sourcePaths = this.getSourceRoots();
        debugConfiguration.ext = this.getExt();

        if (!debugConfiguration.request) {
            debugConfiguration.request = 'launch';
            debugConfiguration.type = 'emmylua_new';
            debugConfiguration.ideConnectDebugger = true;
            debugConfiguration.host = '127.0.0.1';
            debugConfiguration.port = 9966;
        }

        const port = await this.pickPort(debugConfiguration.port);
        if (port === undefined) {
            throw new Error('Debug cancelled by user');
        }

        debugConfiguration.port = port;
        this.saveRecentPort(port);
        return debugConfiguration;
    }

    protected async onDebugCustomEvent(e: vscode.DebugSessionCustomEvent) {
        if (e.event === 'showWaitConnection') {
            this.showWaitConnectionToken.cancel();
            this.showWaitConnectionToken = new vscode.CancellationTokenSource();
            this.showWaitConnection(e.session, this.showWaitConnectionToken.token);
        } else if (e.event === 'onNewConnection') {
            this.showWaitConnectionToken.cancel();
        } else if (e.event === 'connectionFailed') {
            vscode.window.showErrorMessage(e.body.message);
        } else {
            return super.onDebugCustomEvent(e);
        }
    }

    private async showWaitConnection(session: vscode.DebugSession, token: vscode.CancellationToken) {
        return vscode.window.withProgress({
            location: vscode.ProgressLocation.Notification,
            title: 'Wait for connection.',
            cancellable: true
        }, async (_progress, userCancelToken) => {
            userCancelToken.onCancellationRequested(() => {
                session.customRequest('stopWaitConnection');
            });
            await new Promise<void>(r => token.onCancellationRequested(() => r()));
        });
    }

    protected onTerminateDebugSession(session: vscode.DebugSession) {
        this.showWaitConnectionToken.cancel();
    }
}
