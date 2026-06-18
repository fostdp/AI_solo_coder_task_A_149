#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '..', 'frontend');
const EXTENSIONS = ['.html', '.js', '.css', '.json', '.svg', '.map'];
const COMPRESSION_LEVEL = 9;

function walkDir(dir, results = []) {
    const items = fs.readdirSync(dir);
    for (const item of items) {
        const fullPath = path.join(dir, item);
        const stat = fs.statSync(fullPath);
        if (stat.isDirectory() && item !== 'node_modules') {
            walkDir(fullPath, results);
        } else if (stat.isFile() && EXTENSIONS.includes(path.extname(item)) && !item.endsWith('.gz')) {
            results.push(fullPath);
        }
    }
    return results;
}

function gzipFile(srcPath) {
    return new Promise((resolve, reject) => {
        const dstPath = srcPath + '.gz';
        const srcStat = fs.statSync(srcPath);

        if (fs.existsSync(dstPath)) {
            const dstStat = fs.statSync(dstPath);
            if (srcStat.mtime <= dstStat.mtime && srcStat.size > 0) {
                console.log(`  skip (up-to-date): ${path.relative(ROOT, srcPath)}`);
                return resolve({ skipped: true, path: srcPath });
            }
        }

        const read = fs.createReadStream(srcPath);
        const write = fs.createWriteStream(dstPath);
        const gzip = zlib.createGzip({ level: COMPRESSION_LEVEL });

        read.pipe(gzip).pipe(write);

        write.on('finish', () => {
            const dstStat = fs.statSync(dstPath);
            const ratio = ((1 - dstStat.size / srcStat.size) * 100).toFixed(1);
            console.log(`  gzip: ${path.relative(ROOT, srcPath)} -> ${dstStat.size} bytes (-${ratio}%)`);
            resolve({ skipped: false, path: srcPath, ratio });
        });
        write.on('error', reject);
        read.on('error', reject);
    });
}

async function main() {
    console.log('\nCompressing frontend static assets with gzip...');
    console.log(`Root: ${ROOT}\n`);

    const files = walkDir(ROOT);
    if (files.length === 0) {
        console.log('No files found to compress.');
        return;
    }

    console.log(`Found ${files.length} files to compress.\n`);

    const results = await Promise.all(files.map(f => gzipFile(f)));
    const compressed = results.filter(r => !r.skipped).length;
    const skipped = results.filter(r => r.skipped).length;

    console.log(`\nDone: ${compressed} compressed, ${skipped} skipped (up-to-date).\n`);
}

main().catch(err => {
    console.error('\nGzip compression failed:', err);
    process.exit(1);
});
