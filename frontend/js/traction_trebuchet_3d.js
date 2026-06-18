const TractionTrebuchet3D = (function () {
    "use strict";

    let scene, camera, renderer, orbitControls;
    let trebuchetGroup, armGroup, projectileMesh, trajectoryLine;
    let isAnimating = false;
    let animationTime = 0;
    let currentArmAngle = 0;
    let targetArmAngle = 0;
    let onFireCompleteCallback = null;
    let containerEl, canvasEl;
    let rafId = null;
    let disposed = false;

    let CONFIG = {
        cameraDistance: 22,
        cameraTheta: 0.9,
        cameraPhi: 1.0,
        cameraTarget: { x: 0, y: 2, z: 0 },
        armBaseAngleRad: -Math.PI / 4,
        armFireAngleRad: Math.PI / 3,
        firePhaseSec: 0.3,
        projectileMassKg: 10,
        useCompressibleDrag: true
    };

    function init(canvasId, options = {}) {
        canvasEl = document.getElementById(canvasId);
        if (!canvasEl) {
            console.error('[TractionTrebuchet3D] canvas not found:', canvasId);
            return false;
        }
        containerEl = canvasEl.parentElement;
        Object.assign(CONFIG, options);

        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x0a0e1a);
        scene.fog = new THREE.Fog(0x0a0e1a, 50, 200);

        const rect = containerEl.getBoundingClientRect();
        camera = new THREE.PerspectiveCamera(50, rect.width / rect.height, 0.1, 1000);
        applyCameraSpherical();

        renderer = new THREE.WebGLRenderer({ canvas: canvasEl, antialias: true, alpha: true });
        renderer.setSize(rect.width, rect.height);
        renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        renderer.shadowMap.enabled = true;
        renderer.shadowMap.type = THREE.PCFSoftShadowMap;

        setupLights();
        setupGround();
        createTrebuchetGeometry();
        createProjectile();
        createTrajectoryLine();
        setupMouseOrbit();

        window.addEventListener('resize', onWindowResize);
        startRenderLoop();
        return true;
    }

    function applyCameraSpherical() {
        const r = CONFIG.cameraDistance;
        const theta = CONFIG.cameraTheta;
        const phi = CONFIG.cameraPhi;
        camera.position.set(
            CONFIG.cameraTarget.x + r * Math.sin(phi) * Math.cos(theta),
            CONFIG.cameraTarget.y + r * Math.cos(phi),
            CONFIG.cameraTarget.z + r * Math.sin(phi) * Math.sin(theta)
        );
        camera.lookAt(CONFIG.cameraTarget.x, CONFIG.cameraTarget.y, CONFIG.cameraTarget.z);
    }

    function setupLights() {
        scene.add(new THREE.AmbientLight(0x404060, 0.5));
        const sun = new THREE.DirectionalLight(0xfff0d4, 1.2);
        sun.position.set(15, 25, 10);
        sun.castShadow = true;
        sun.shadow.mapSize.set(2048, 2048);
        sun.shadow.camera.left = -30;
        sun.shadow.camera.right = 30;
        sun.shadow.camera.top = 30;
        sun.shadow.camera.bottom = -30;
        sun.shadow.camera.near = 0.5;
        sun.shadow.camera.far = 100;
        scene.add(sun);

        const fill = new THREE.DirectionalLight(0x4466aa, 0.3);
        fill.position.set(-10, 10, -10);
        scene.add(fill);
        scene.add(new THREE.HemisphereLight(0x87ceeb, 0x362d1f, 0.3));
    }

    function setupGround() {
        const groundGeo = new THREE.PlaneGeometry(100, 100);
        const groundMat = new THREE.MeshStandardMaterial({
            color: 0x2d3d2a, roughness: 0.9, metalness: 0.0
        });
        const ground = new THREE.Mesh(groundGeo, groundMat);
        ground.rotation.x = -Math.PI / 2;
        ground.receiveShadow = true;
        scene.add(ground);

        const grid = new THREE.GridHelper(60, 60, 0x3a4a6a, 0x253048);
        grid.position.y = 0.01;
        scene.add(grid);
    }

    function woodMat(dark = false) {
        return new THREE.MeshStandardMaterial({
            color: dark ? 0x3d2817 : 0x5c3d24,
            roughness: 0.75, metalness: 0.05
        });
    }

    function metalMat() {
        return new THREE.MeshStandardMaterial({
            color: 0x4a4a4a, roughness: 0.35, metalness: 0.85
        });
    }

    function createTrebuchetGeometry() {
        trebuchetGroup = new THREE.Group();
        scene.add(trebuchetGroup);

        const base = new THREE.Mesh(new THREE.BoxGeometry(8, 0.5, 2.5), woodMat(true));
        base.position.y = 0.25;
        base.castShadow = base.receiveShadow = true;
        trebuchetGroup.add(base);

        for (let i = 0; i < 4; i++) {
            const cross = new THREE.Mesh(new THREE.BoxGeometry(8, 0.3, 0.3), woodMat());
            cross.position.set(0, 0.65, -1.1 + i * 0.75);
            cross.castShadow = cross.receiveShadow = true;
            trebuchetGroup.add(cross);
        }

        const aFrameLeft = new THREE.Group();
        aFrameLeft.position.set(0, 0.5, 1.2);
        aFrameLeft.rotation.y = -0.2;
        const pole1 = new THREE.Mesh(new THREE.CylinderGeometry(0.15, 0.15, 5.5), woodMat());
        pole1.position.set(1.5, 2.75, 0);
        pole1.rotation.z = -0.45;
        pole1.castShadow = true;
        aFrameLeft.add(pole1);
        const pole2 = pole1.clone();
        pole2.position.set(-1.5, 2.75, 0);
        pole2.rotation.z = 0.45;
        aFrameLeft.add(pole2);
        const topBeam = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.2, 3.6), woodMat(true));
        topBeam.position.set(0, 4.2, 0);
        topBeam.rotation.z = Math.PI / 2;
        topBeam.castShadow = true;
        aFrameLeft.add(topBeam);
        trebuchetGroup.add(aFrameLeft);

        const aFrameRight = aFrameLeft.clone();
        aFrameRight.position.z = -1.2;
        aFrameRight.rotation.y = 0.2;
        trebuchetGroup.add(aFrameRight);

        armGroup = new THREE.Group();
        armGroup.position.y = 4.2;
        trebuchetGroup.add(armGroup);

        const arm = new THREE.Mesh(new THREE.BoxGeometry(0.4, 0.4, 8), woodMat());
        arm.position.set(0, 0, 1.8);
        arm.castShadow = arm.receiveShadow = true;
        armGroup.add(arm);

        const sling = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.5, 0.8), metalMat());
        sling.position.set(0, -0.2, 5.6);
        sling.castShadow = true;
        armGroup.add(sling);

        const counterBox = new THREE.Mesh(new THREE.BoxGeometry(2, 2, 2),
            new THREE.MeshStandardMaterial({ color: 0x4a4a4a, roughness: 0.8, metalness: 0.2 }));
        counterBox.position.set(0, 0, -2.5);
        counterBox.castShadow = counterBox.receiveShadow = true;
        armGroup.add(counterBox);

        armGroup.rotation.x = CONFIG.armBaseAngleRad;
        currentArmAngle = CONFIG.armBaseAngleRad;
    }

    function createProjectile() {
        const geo = new THREE.SphereGeometry(0.25, 24, 16);
        const mat = new THREE.MeshStandardMaterial({
            color: 0x888888, roughness: 0.8, metalness: 0.1
        });
        projectileMesh = new THREE.Mesh(geo, mat);
        projectileMesh.castShadow = true;
        projectileMesh.receiveShadow = true;
        projectileMesh.visible = false;
        scene.add(projectileMesh);
    }

    function createTrajectoryLine() {
        const geo = new THREE.BufferGeometry();
        const mat = new THREE.LineBasicMaterial({
            color: 0xff6b35, linewidth: 2,
            transparent: true, opacity: 0.85
        });
        trajectoryLine = new THREE.Line(geo, mat);
        trajectoryLine.frustumCulled = false;
        scene.add(trajectoryLine);
    }

    function setupMouseOrbit() {
        orbitControls = orbitControls || {
            dragging: false, lastX: 0, lastY: 0, wheeling: false
        };
        canvasEl.addEventListener('mousedown', (e) => {
            orbitControls.dragging = true;
            orbitControls.lastX = e.clientX;
            orbitControls.lastY = e.clientY;
        });
        window.addEventListener('mouseup', () => { orbitControls.dragging = false; });
        window.addEventListener('mousemove', (e) => {
            if (!orbitControls.dragging) return;
            const dx = e.clientX - orbitControls.lastX;
            const dy = e.clientY - orbitControls.lastY;
            CONFIG.cameraTheta -= dx * 0.005;
            CONFIG.cameraPhi = Math.max(0.15, Math.min(Math.PI / 2 - 0.05,
                CONFIG.cameraPhi - dy * 0.005));
            applyCameraSpherical();
            orbitControls.lastX = e.clientX;
            orbitControls.lastY = e.clientY;
        });
        canvasEl.addEventListener('wheel', (e) => {
            e.preventDefault();
            CONFIG.cameraDistance = Math.max(6, Math.min(80,
                CONFIG.cameraDistance + e.deltaY * 0.02));
            applyCameraSpherical();
        }, { passive: false });
    }

    function onWindowResize() {
        if (!renderer || !containerEl || disposed) return;
        const rect = containerEl.getBoundingClientRect();
        camera.aspect = rect.width / rect.height;
        camera.updateProjectionMatrix();
        renderer.setSize(rect.width, rect.height);
    }

    function startRenderLoop() {
        const step = function () {
            if (disposed) return;
            renderFrame();
            rafId = requestAnimationFrame(step);
        };
        rafId = requestAnimationFrame(step);
    }

    function renderFrame() {
        if (isAnimating) {
            animationTime += 1 / 60;
            const t = Math.min(animationTime / CONFIG.firePhaseSec, 1);
            targetArmAngle = CONFIG.armBaseAngleRad +
                (CONFIG.armFireAngleRad - CONFIG.armBaseAngleRad) * t;
            const smooth = easeOutCubic(t);
            currentArmAngle = CONFIG.armBaseAngleRad +
                (CONFIG.armFireAngleRad - CONFIG.armBaseAngleRad) * smooth;
            armGroup.rotation.x = currentArmAngle;

            if (t >= 1 && !projectileMesh.userData.launched) {
                launchProjectileInWorld();
            }
            if (projectileMesh.userData.launched) {
                integrateProjectile();
            }
            if (projectileMesh.userData.landed && animationTime > CONFIG.firePhaseSec + 1.2) {
                isAnimating = false;
                projectileMesh.userData.landed = false;
                projectileMesh.userData.launched = false;
                projectileMesh.visible = false;
                armGroup.rotation.x = CONFIG.armBaseAngleRad;
                currentArmAngle = CONFIG.armBaseAngleRad;
                if (onFireCompleteCallback) {
                    const cb = onFireCompleteCallback;
                    onFireCompleteCallback = null;
                    setTimeout(() => cb(projectileMesh.userData.lastResult || {}), 30);
                }
            }
        }
        renderer.render(scene, camera);
    }

    function easeOutCubic(t) { return 1 - Math.pow(1 - t, 3); }

    function getProjectileReleaseState() {
        const slingOffset = new THREE.Vector3(0, -0.2, 5.6);
        slingOffset.applyEuler(new THREE.Euler(currentArmAngle, 0, 0));
        slingOffset.add(new THREE.Vector3(0, 4.2, 0));
        slingOffset.applyMatrix4(trebuchetGroup.matrixWorld);
        const pos = slingOffset.clone();
        const omega = (CONFIG.armFireAngleRad - CONFIG.armBaseAngleRad) / CONFIG.firePhaseSec;
        const r = 5.6;
        const vMagnitude = omega * r;
        const normalAngle = currentArmAngle + Math.PI / 2;
        const vx = 0;
        const vy = vMagnitude * Math.sin(normalAngle);
        const vz = vMagnitude * Math.cos(normalAngle);
        return {
            position: { x: pos.x, y: pos.y, z: pos.z },
            velocity: { x: vx, y: vy, z: vz }
        };
    }

    function launchProjectileInWorld() {
        const rel = getProjectileReleaseState();
        projectileMesh.userData.launched = true;
        projectileMesh.visible = true;
        projectileMesh.position.set(rel.position.x, rel.position.y, rel.position.z);

        const launchAngleDeg = Math.atan2(rel.velocity.y, Math.abs(rel.velocity.z)) * 180 / Math.PI;
        const velocity = Math.sqrt(rel.velocity.x * rel.velocity.x +
            rel.velocity.y * rel.velocity.y +
            rel.velocity.z * rel.velocity.z);
        const cfg = {
            mass: CONFIG.projectileMassKg,
            dragCoefficient: 0.47,
            crossSectionArea: Math.PI * Math.pow(0.1, 2)
        };
        const projectile = { ...cfg, diameter: 0.2 };
        const result = TrebuchetPhysics.calculateFullTrajectory(
            projectile, velocity, launchAngleDeg, 1.0
        );
        projectileMesh.userData.pos = { x: 0, y: rel.position.y, z: 0 };
        projectileMesh.userData.vel = {
            x: rel.velocity.x,
            y: Math.abs(rel.velocity.z) > 0.1 ? Math.sin(launchAngleDeg * Math.PI / 180) * velocity : rel.velocity.y,
            z: 0
        };
        projectileMesh.userData.forward = Math.sign(rel.velocity.z) || 1;
        projectileMesh.userData.lastResult = result;
        projectileMesh.userData.trail = [];
        updateTrajectoryLineWith(result);
    }

    function integrateProjectile() {
        const dt = 1 / 60;
        const state = projectileMesh.userData;
        if (!state.vel) return;

        if (CONFIG.useCompressibleDrag && window.TrebuchetPhysics) {
            const v = Math.sqrt(state.vel.x * state.vel.x + state.vel.y * state.vel.y + state.vel.z * state.vel.z);
            const T = 288.15;
            const Ma = TrebuchetPhysics.calculateMachNumber(v, T);
            const Re = TrebuchetPhysics.calculateMachNumber ? (1.225 * v * 0.2 / 1.716e-5) : 1e6;
            const Cd = TrebuchetPhysics.calculateCompressibleDragCoefficient(Ma, 0.47, Re);
            const dragAcc = 0.5 * 1.225 * Cd * Math.PI * 0.1 * 0.1 / CONFIG.projectileMassKg * v;
            state.vel.x -= dragAcc * (state.vel.x / Math.max(v, 0.001)) * dt;
            state.vel.y -= dragAcc * (state.vel.y / Math.max(v, 0.001)) * dt;
            state.vel.z -= dragAcc * (state.vel.z / Math.max(v, 0.001)) * dt;
        }
        state.vel.y -= 9.80665 * dt;
        state.pos.x += state.vel.x * dt;
        state.pos.y += state.vel.y * dt;
        state.pos.z += state.vel.z * dt;

        projectileMesh.position.set(
            state.pos.x, state.pos.y, state.pos.z * (state.forward || 1)
        );
        projectileMesh.rotation.y += 0.1;

        if (state.pos.y <= 0.05) {
            state.pos.y = 0;
            projectileMesh.position.y = 0;
            state.landed = true;
        }
        state.trail = state.trail || [];
        state.trail.push([
            projectileMesh.position.x,
            projectileMesh.position.y,
            projectileMesh.position.z
        ]);
        if (state.trail.length > 500) state.trail.shift();
        renderProjectileTrail();
    }

    function renderProjectileTrail() {
        const trail = projectileMesh.userData.trail;
        if (!trail || trail.length < 2) return;
        const arr = new Float32Array(trail.length * 3);
        for (let i = 0; i < trail.length; i++) {
            arr[i * 3 + 0] = trail[i][0];
            arr[i * 3 + 1] = trail[i][1];
            arr[i * 3 + 2] = trail[i][2];
        }
        trajectoryLine.geometry.setAttribute('position', new THREE.BufferAttribute(arr, 3));
        trajectoryLine.geometry.computeBoundingSphere();
    }

    function updateTrajectoryLineWith(result) {
        if (!result || !result.trajectoryPoints) return;
        const pts = result.trajectoryPoints;
        if (pts.length < 2) return;
        const arr = new Float32Array(pts.length * 3);
        for (let i = 0; i < pts.length; i++) {
            arr[i * 3 + 0] = pts[i].x;
            arr[i * 3 + 1] = pts[i].y;
            arr[i * 3 + 2] = 0;
        }
        trajectoryLine.geometry.setAttribute('position', new THREE.BufferAttribute(arr, 3));
        trajectoryLine.geometry.computeBoundingSphere();
    }

    function fireProjectile(initialMassKg, callback) {
        if (isAnimating) return false;
        CONFIG.projectileMassKg = initialMassKg || CONFIG.projectileMassKg;
        isAnimating = true;
        animationTime = 0;
        onFireCompleteCallback = callback || null;
        projectileMesh.userData = { launched: false, landed: false };
        projectileMesh.visible = false;
        return true;
    }

    function setArmAngle(angleRad) {
        if (isAnimating) return;
        currentArmAngle = angleRad;
        armGroup.rotation.x = angleRad;
    }

    function getStateSnapshot() {
        return {
            animating: isAnimating,
            armAngleRad: currentArmAngle,
            cameraDistance: CONFIG.cameraDistance,
            cameraTheta: CONFIG.cameraTheta,
            cameraPhi: CONFIG.cameraPhi,
            projectileMassKg: CONFIG.projectileMassKg
        };
    }

    function dispose() {
        disposed = true;
        if (rafId) cancelAnimationFrame(rafId);
        window.removeEventListener('resize', onWindowResize);
        scene.traverse((obj) => {
            if (obj.geometry && obj.geometry.dispose) obj.geometry.dispose();
            if (obj.material) {
                if (Array.isArray(obj.material)) obj.material.forEach(m => m.dispose && m.dispose());
                else if (obj.material.dispose) obj.material.dispose();
            }
        });
        if (renderer) renderer.dispose();
    }

    return {
        init,
        fireProjectile,
        setArmAngle,
        getStateSnapshot,
        applyCameraSpherical,
        dispose,
        CONFIG
    };
})();
