import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

let chromium;
try {
	({chromium} = await import('playwright'));
} catch (error) {
	console.error('The "playwright" package is required. Install it with: npm install playwright && npx playwright install chromium');
	process.exit(2);
}

const args = process.argv.slice(2);
const htmlPath = args[0];
const filterArg = args.find((arg) => arg.startsWith('--filter='));
const filter = filterArg ? filterArg.substring('--filter='.length) : 'WebGPUBrowser.*';

if (!htmlPath) {
	console.error('Usage: node run_webgpu_browser_test.mjs <LLGI_Test.html> [--filter=WebGPUBrowser.*]');
	process.exit(2);
}

const resolvedHtmlPath = path.resolve(htmlPath);
if (!fs.existsSync(resolvedHtmlPath)) {
	console.error(`Not found: ${resolvedHtmlPath}`);
	process.exit(2);
}

const root = path.dirname(resolvedHtmlPath);
const htmlFile = path.basename(resolvedHtmlPath);
const executablePath = process.env.CHROME_PATH || process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH;

function contentType(filePath) {
	if (filePath.endsWith('.html')) return 'text/html';
	if (filePath.endsWith('.js')) return 'application/javascript';
	if (filePath.endsWith('.wasm')) return 'application/wasm';
	if (filePath.endsWith('.data')) return 'application/octet-stream';
	return 'application/octet-stream';
}

const server = http.createServer((request, response) => {
	const requestUrl = new URL(request.url, 'http://127.0.0.1');
	if (requestUrl.pathname === '/favicon.ico') {
		response.writeHead(204);
		response.end();
		return;
	}

	const relativePath = decodeURIComponent(requestUrl.pathname === '/' ? `/${htmlFile}` : requestUrl.pathname);
	const filePath = path.resolve(root, `.${relativePath}`);

	if (!filePath.startsWith(root) || !fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
		response.writeHead(404);
		response.end('Not found');
		return;
	}

	response.writeHead(200, {
		'Content-Type': contentType(filePath),
		'Cache-Control': 'no-store, max-age=0',
		'Pragma': 'no-cache',
		'Expires': '0',
	});
	fs.createReadStream(filePath).pipe(response);
});

await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const {port} = server.address();
const url = `http://127.0.0.1:${port}/${htmlFile}?filter=${encodeURIComponent(filter)}`;

let browser;
try {
	browser = await chromium.launch({
		headless: true,
		executablePath,
		args: [
			'--enable-unsafe-webgpu',
			'--ignore-gpu-blocklist',
			'--enable-features=Vulkan,UseSkiaRenderer',
			'--use-vulkan=swiftshader',
		],
	});

	const page = await browser.newPage();
	page.on('console', (message) => console.log(`[browser:${message.type()}] ${message.text()}`));
	page.on('pageerror', (error) => console.error(`[browser:pageerror] ${error.message}`));

	await page.goto(url, {waitUntil: 'load'});
	const result = await page.waitForFunction(
		() => globalThis.Module && globalThis.Module.llgiTestResult,
		null,
		{timeout: 60000}
	);
	const value = await result.jsonValue();
	if (!value || value.status !== 'passed') {
		console.error(value && value.message ? value.message : 'LLGI browser WebGPU test failed.');
		process.exitCode = 1;
	} else {
		await page.waitForTimeout(500);
		const lateError = await page.evaluate(() => globalThis.Module && globalThis.Module.llgiLastWebGPUError);
		if (lateError) {
			console.error(lateError);
			process.exitCode = 1;
		}
	}
} finally {
	if (browser) {
		await browser.close();
	}
	await new Promise((resolve) => server.close(resolve));
}
